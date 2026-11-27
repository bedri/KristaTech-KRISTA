#include "script/mescal.h"
#include "pubkey.h"
#include "utilstrencodings.h"
#include "uint256.h"
#include <algorithm>
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>

static const int MAX_MESCAL_CONDITION_DEPTH = 32;

// Helper to check if a vector is a valid pubkey (33 bytes compressed or 65 bytes uncompressed)
static bool IsPubKey(const std::vector<unsigned char>& vch) {
    return (vch.size() == 33 && (vch[0] == 0x02 || vch[0] == 0x03)) ||
           (vch.size() == 65 && vch[0] == 0x04);
}

static bool ParseValidatedPubKey(const std::string& hexStr, std::vector<unsigned char>& out, std::string& errorStr) {
    if (!IsHex(hexStr)) {
        errorStr = "Public key must be a valid hex string";
        return false;
    }
    out = ParseHex(hexStr);
    if (!IsPubKey(out)) {
        errorStr = "Public key must be 33-byte compressed (02/03) or 65-byte uncompressed (04)";
        return false;
    }
    CPubKey pk(out);
    if (!pk.IsFullyValid()) {
        errorStr = "Public key is not a valid secp256k1 curve point";
        return false;
    }
    return true;
}

static bool ParseValidatedHash160(const std::string& hexStr, std::vector<unsigned char>& out, std::string& errorStr) {
    if (!IsHex(hexStr) || hexStr.size() != 40) {
        errorStr = "Hash160 / pubkeyhash must be a 40-character (20-byte) hex string";
        return false;
    }
    out = ParseHex(hexStr);
    if (out.size() != 20) {
        errorStr = "Hash160 / pubkeyhash must decode to 20 bytes";
        return false;
    }
    return true;
}

// Helper to validate lock-time values against 32-bit nLockTime bounds
static bool ParseValidatedLockTime(const UniValue& inputObj, int64_t& lockTimeOut, std::string& errorStr) {
    if (!inputObj.exists("value") || !inputObj["value"].isNum()) {
        errorStr = "lock-time value must be an integer (block height or UNIX timestamp)";
        return false;
    }
    int64_t val = 0;
    try {
        val = inputObj["value"].get_int64();
    } catch (...) {
        errorStr = "lock-time value must be an integer (block height or UNIX timestamp)";
        return false;
    }
    if (val < 0 || val > 0xFFFFFFFFLL) {
        errorStr = "lock-time value must be between 0 and 4294967295 (32-bit nLockTime limit)";
        return false;
    }
    lockTimeOut = val;
    return true;
}

static CScript CompileBasicBlock(const UniValue& blockObj, const std::map<std::string, UniValue>& basicBlocks, std::string& errorStr, bool inConditionExpr = false) {
    CScript script;
    if (!blockObj.isObject() || !blockObj.exists("role") || !blockObj["role"].isStr()) {
        errorStr = "Basic block must contain a string 'role'";
        return CScript();
    }
    std::string role = blockObj["role"].get_str();
    UniValue inputs = blockObj.exists("inputs") ? blockObj["inputs"] : UniValue(UniValue::VARR);

    if (role == "hash160") {
        if (!inputs.isArray() || inputs.size() < 1 || !inputs[0].isObject() || !inputs[0].exists("value")) {
            errorStr = "hash160 requires at least 1 input";
            return CScript();
        }
        UniValue input = inputs[0];
        std::string valStr = input["value"].isStr() ? input["value"].get_str() : input["value"].getValStr();
        const std::string mode = (blockObj.exists("mode") && blockObj["mode"].isStr()) ? blockObj["mode"].get_str() : "";
        bool isHashEqualVerify = (mode == "hash-equalverify");
        bool isHtlcEqual = (mode == "htlc-equal") ||
                           (!isHashEqualVerify &&
                            ((input.exists("name") && input["name"].isStr() && input["name"].get_str() == "hash160") ||
                             (input.exists("type") && input["type"].isStr() && input["type"].get_str() == "hash160")));
        if (inConditionExpr && (isHashEqualVerify || !isHtlcEqual)) {
            errorStr = "hash160 inside if-condition expressions must use htlc-equal mode (OP_HASH160 <hash20> OP_EQUAL)";
            return CScript();
        }
        if (isHashEqualVerify || isHtlcEqual) {
            std::vector<unsigned char> hashBytes;
            if (!ParseValidatedHash160(valStr, hashBytes, errorStr)) {
                return CScript();
            }
            script << OP_HASH160 << hashBytes << (isHashEqualVerify ? OP_EQUALVERIFY : OP_EQUAL);
        } else {
            std::vector<unsigned char> rawData;
            if (IsHex(valStr)) {
                rawData = ParseHex(valStr);
            } else {
                rawData.assign(valStr.begin(), valStr.end());
            }
            if (rawData.size() > MAX_SCRIPT_ELEMENT_SIZE) {
                errorStr = "hash160 data exceeds MAX_SCRIPT_ELEMENT_SIZE (520 bytes)";
                return CScript();
            }
            script << rawData << OP_HASH160;
        }
    } else if (role == "check-signature-verification") {
        if (!inputs.isArray() || inputs.size() < 1 || !inputs[0].isObject() || !inputs[0].exists("value") || !inputs[0]["value"].isStr()) {
            errorStr = "check-signature-verification requires a hex public key input";
            return CScript();
        }
        std::vector<unsigned char> pubkeyBytes;
        if (!ParseValidatedPubKey(inputs[0]["value"].get_str(), pubkeyBytes, errorStr)) {
            return CScript();
        }
        bool useVerify = !inConditionExpr;
        if (blockObj.exists("verify")) {
            if (!blockObj["verify"].isBool()) {
                errorStr = "check-signature-verification 'verify' field must be a boolean";
                return CScript();
            }
            bool requestedVerify = blockObj["verify"].get_bool();
            if (inConditionExpr && requestedVerify) {
                errorStr = "check-signature-verification inside if-condition expressions cannot set verify=true";
                return CScript();
            }
            useVerify = requestedVerify;
        }
        script << pubkeyBytes;
        script << (useVerify ? OP_CHECKSIGVERIFY : OP_CHECKSIG);
    } else if (role == "equalverify-checksig") {
        if (!inputs.isArray() || inputs.size() < 1 || !inputs[0].isObject() || !inputs[0].exists("value") || !inputs[0]["value"].isStr()) {
            errorStr = "equalverify-checksig requires a hex pubkeyhash input";
            return CScript();
        }
        std::vector<unsigned char> pubkeyHashBytes;
        if (!ParseValidatedHash160(inputs[0]["value"].get_str(), pubkeyHashBytes, errorStr)) {
            return CScript();
        }
        script << OP_DUP << OP_HASH160 << pubkeyHashBytes << OP_EQUALVERIFY << OP_CHECKSIG;
    } else if (role == "number") {
        if (!inputs.isArray() || inputs.size() < 1 || !inputs[0].isObject() || !inputs[0].exists("value") || !inputs[0]["value"].isNum()) {
            errorStr = "number requires an integer input";
            return CScript();
        }
        int64_t val = inputs[0]["value"].get_int64();
        if (val < -2147483647LL || val > 2147483647LL) {
            errorStr = "number value exceeds signed 4-byte CScriptNum range [-2147483647, 2147483647]";
            return CScript();
        }
        script << val;
    } else if (role == "multi-signature") {
        if (!inputs.isArray()) {
            errorStr = "multi-signature requires an inputs array";
            return CScript();
        }
        int64_t m = 0, n = 0;
        UniValue sigs;
        for (unsigned int i = 0; i < inputs.size(); i++) {
            if (!inputs[i].isObject() || !inputs[i].exists("name") || !inputs[i]["name"].isStr()) continue;
            std::string name = inputs[i]["name"].get_str();
            if (name == "m" && inputs[i].exists("value") && inputs[i]["value"].isNum()) m = inputs[i]["value"].get_int64();
            else if (name == "n" && inputs[i].exists("value") && inputs[i]["value"].isNum()) n = inputs[i]["value"].get_int64();
            else if (name == "Signatures" && inputs[i].exists("value")) sigs = inputs[i]["value"];
        }
        if (!sigs.isArray() || m <= 0 || n <= 0 || m > n || n > 20 || sigs.size() != static_cast<size_t>(n)) {
            errorStr = "multi-signature requires 1 <= m <= n <= 20 and Signatures array matching n";
            return CScript();
        }
        script << m;
        for (unsigned int i = 0; i < sigs.size(); i++) {
            if (!sigs[i].isStr()) {
                errorStr = "multi-signature public key must be a hex string";
                return CScript();
            }
            std::vector<unsigned char> pkBytes;
            if (!ParseValidatedPubKey(sigs[i].get_str(), pkBytes, errorStr)) {
                return CScript();
            }
            script << pkBytes;
        }
        script << n << OP_CHECKMULTISIG;
    } else if (role == "lock-time") {
        if (inConditionExpr) {
            errorStr = "lock-time cannot be used inside an if-condition expression";
            return CScript();
        }
        if (!inputs.isArray() || inputs.size() < 1) {
            errorStr = "lock-time requires lock-time value";
            return CScript();
        }
        int64_t lockTime = 0;
        if (!ParseValidatedLockTime(inputs[0], lockTime, errorStr)) {
            return CScript();
        }
        script << CScriptNum(lockTime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    } else if (role == "add") {
        script << OP_ADD;
    } else if (role == "sub") {
        script << OP_SUB;
    } else if (role == "bool-and") {
        script << OP_BOOLAND;
    } else if (role == "bool-or") {
        script << OP_BOOLOR;
    } else if (role == "num-equal") {
        script << OP_NUMEQUAL;
    } else if (role == "greater-than") {
        script << OP_GREATERTHAN;
    } else if (role == "drop") {
        if (inConditionExpr) {
            errorStr = "drop cannot be used inside an if-condition expression";
            return CScript();
        }
        script << OP_DROP;
    } else if (role == "coin-lock-miner") {
        if (inConditionExpr) {
            errorStr = "coin-lock-miner cannot be used inside an if-condition expression";
            return CScript();
        }
        if (!inputs.isArray() || inputs.size() < 3 ||
            !inputs[0].exists("value") || !inputs[0]["value"].isStr() ||
            !inputs[2].exists("value") || !inputs[2]["value"].isStr()) {
            errorStr = "coin-lock-miner requires pubkey, lock-time, and pubkeyhash";
            return CScript();
        }
        std::vector<unsigned char> pubkeyBytes;
        if (!ParseValidatedPubKey(inputs[0]["value"].get_str(), pubkeyBytes, errorStr)) {
            return CScript();
        }
        if (pubkeyBytes.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
            errorStr = "coin-lock-miner requires a 33-byte compressed public key";
            return CScript();
        }
        int64_t lockTime = 0;
        if (!ParseValidatedLockTime(inputs[1], lockTime, errorStr)) {
            return CScript();
        }
        std::vector<unsigned char> pubkeyHashBytes;
        if (!ParseValidatedHash160(inputs[2]["value"].get_str(), pubkeyHashBytes, errorStr)) {
            return CScript();
        }
        script << pubkeyBytes << OP_DROP << CScriptNum(lockTime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
               << OP_DUP << OP_HASH160 << pubkeyHashBytes << OP_EQUALVERIFY << OP_CHECKSIG;
    } else if (role == "pow-miner") {
        if (inConditionExpr) {
            errorStr = "pow-miner cannot be used inside an if-condition expression";
            return CScript();
        }
        if (!inputs.isArray() || inputs.size() < 5 ||
            !inputs[0].exists("value") || !inputs[0]["value"].isStr() ||
            !inputs[1].exists("value") || !inputs[1]["value"].isStr() ||
            !inputs[2].exists("value") || !inputs[2]["value"].isStr() ||
            !inputs[4].exists("value") || !inputs[4]["value"].isStr()) {
            errorStr = "pow-miner requires nonce, challenge, pubkey, lock-time, and pubkeyhash";
            return CScript();
        }
        std::string nonceHex = inputs[0]["value"].get_str();
        std::string challengeHex = inputs[1]["value"].get_str();
        if (!IsHex(nonceHex) || (nonceHex.size() % 2) != 0 ||
            !IsHex(challengeHex) || challengeHex.size() != 64) {
            errorStr = "pow-miner nonce must be even-length hex and challenge must be 64-char hex";
            return CScript();
        }
        std::vector<unsigned char> nonceBytes = ParseHex(nonceHex);
        std::vector<unsigned char> challengeBytes = ParseHex(challengeHex);
        if (nonceBytes.empty() || nonceBytes.size() > MAX_SCRIPT_ELEMENT_SIZE || challengeBytes.size() != 32) {
            errorStr = "pow-miner nonce must be 1..520 bytes and challenge must be 32 bytes";
            return CScript();
        }
        std::vector<unsigned char> pubkeyBytes;
        if (!ParseValidatedPubKey(inputs[2]["value"].get_str(), pubkeyBytes, errorStr)) {
            return CScript();
        }
        if (pubkeyBytes.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
            errorStr = "pow-miner requires a 33-byte compressed public key";
            return CScript();
        }
        int64_t lockTime = 0;
        if (!ParseValidatedLockTime(inputs[3], lockTime, errorStr)) {
            return CScript();
        }
        std::vector<unsigned char> pubkeyHashBytes;
        if (!ParseValidatedHash160(inputs[4]["value"].get_str(), pubkeyHashBytes, errorStr)) {
            return CScript();
        }
        script << nonceBytes << challengeBytes << pubkeyBytes << OP_DROP << OP_DROP << OP_DROP
               << CScriptNum(lockTime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
               << OP_DUP << OP_HASH160 << pubkeyHashBytes << OP_EQUALVERIFY << OP_CHECKSIG;
    } else if (role == "stack-input") {
        if (!inConditionExpr) {
            errorStr = "stack-input can only be used inside an if-condition expression";
            return CScript();
        }
        // Expects boolean expression value from witness/scriptSig stack before OP_IF
    } else {
        errorStr = "Unknown basic block role: " + role;
    }
    return script;
}

static CScript CompileAction(const UniValue& action, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr, int depth, size_t& nSteps, bool inConditionExpr = false);

static CScript CompileCondition(const UniValue& condObj, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr, int depth, size_t& nSteps) {
    if (depth > 32) {
        errorStr = "Maximum MESCAL recursion depth exceeded";
        return CScript();
    }
    if (++nSteps > 2048) {
        errorStr = "Maximum MESCAL compilation complexity exceeded";
        return CScript();
    }
    if (!condObj.isObject()) {
        errorStr = "if-condition block must be a JSON object";
        return CScript();
    }
    CScript script;
    UniValue exprs = condObj.exists("expressions") ? condObj["expressions"] : UniValue();
    if (!exprs.isArray() || exprs.empty()) {
        errorStr = "if-condition requires a non-empty expressions array";
        return CScript();
    }
    UniValue trueBranch = condObj.exists("true") ? condObj["true"] : UniValue(UniValue::VARR);
    UniValue falseBranch = condObj.exists("false") ? condObj["false"] : UniValue(UniValue::VARR);
    if (!trueBranch.isArray() || !falseBranch.isArray()) {
        errorStr = "if-condition 'true' and 'false' branches must be arrays";
        return CScript();
    }
    if (trueBranch.empty() && falseBranch.empty()) {
        errorStr = "if-condition requires at least one non-empty branch ('true' or 'false')";
        return CScript();
    }

    // Compile expressions (must evaluate to booleans on stack for OP_IF)
    for (unsigned int i = 0; i < exprs.size(); i++) {
        CScript exprScript = CompileAction(exprs[i], basicBlocks, conditions, errorStr, depth + 1, nSteps, true);
        if (!errorStr.empty()) return CScript();
        script += exprScript;
        if (script.size() > MAX_SCRIPT_SIZE) {
            errorStr = "Compiled MESCAL script exceeds MAX_SCRIPT_SIZE";
            return CScript();
        }
        if (i > 0) {
            script << OP_BOOLAND; // Combine expressions with AND
        }
    }

    script << OP_IF;

    // Compile True branch
    for (unsigned int i = 0; i < trueBranch.size(); i++) {
        CScript trueScript = CompileAction(trueBranch[i], basicBlocks, conditions, errorStr, depth + 1, nSteps, false);
        if (!errorStr.empty()) return CScript();
        script += trueScript;
        if (script.size() > MAX_SCRIPT_SIZE) {
            errorStr = "Compiled MESCAL script exceeds MAX_SCRIPT_SIZE";
            return CScript();
        }
    }

    // Compile False branch if present
    if (falseBranch.size() > 0) {
        script << OP_ELSE;
        for (unsigned int i = 0; i < falseBranch.size(); i++) {
            CScript falseScript = CompileAction(falseBranch[i], basicBlocks, conditions, errorStr, depth + 1, nSteps, false);
            if (!errorStr.empty()) return CScript();
            script += falseScript;
            if (script.size() > MAX_SCRIPT_SIZE) {
                errorStr = "Compiled MESCAL script exceeds MAX_SCRIPT_SIZE";
                return CScript();
            }
        }
    }

    script << OP_ENDIF;
    return script;
}

static CScript CompileAction(const UniValue& action, const std::map<std::string, UniValue>& basicBlocks, const std::map<std::string, UniValue>& conditions, std::string& errorStr, int depth, size_t& nSteps, bool inConditionExpr) {
    if (depth > 32) {
        errorStr = "Maximum MESCAL recursion depth exceeded";
        return CScript();
    }
    if (++nSteps > 2048) {
        errorStr = "Maximum MESCAL compilation complexity exceeded";
        return CScript();
    }
    if (!action.isObject() || !action.exists("type") || !action["type"].isStr()) {
        errorStr = "MESCAL action must be an object with a string 'type'";
        return CScript();
    }
    std::string type = action["type"].get_str();
    std::string name = (action.exists("name") && action["name"].isStr()) ? action["name"].get_str() : "";

    if (type == "basic") {
        auto it = basicBlocks.find(name);
        if (it != basicBlocks.end()) {
            return CompileBasicBlock(it->second, basicBlocks, errorStr, inConditionExpr);
        } else {
            // Check if inline definition
            if (action.exists("role")) {
                return CompileBasicBlock(action, basicBlocks, errorStr, inConditionExpr);
            }
            errorStr = "Basic block definition not found: " + name;
            return CScript();
        }
    } else if (type == "condition") {
        auto it = conditions.find(name);
        if (it != conditions.end()) {
            return CompileCondition(it->second, basicBlocks, conditions, errorStr, depth + 1, nSteps);
        } else {
            // Check if inline definition
            if (action.exists("role") && action["role"].isStr() && action["role"].get_str() == "if-condition") {
                return CompileCondition(action, basicBlocks, conditions, errorStr, depth + 1, nSteps);
            }
            errorStr = "Condition block definition not found: " + name;
            return CScript();
        }
    }
    errorStr = "Unknown action type: " + type;
    return CScript();
}

CScript CMescal::Compile(const std::string& jsonStr, std::string& errorStr) {
    if (jsonStr.size() > 1000000) {
        errorStr = "MESCAL JSON input exceeds maximum size";
        return CScript();
    }
    try {
        UniValue root;
        if (!root.read(jsonStr)) {
            errorStr = "Failed to parse JSON string";
            return CScript();
        }

        std::map<std::string, UniValue> basicBlocks;
        std::map<std::string, UniValue> conditions;
        std::map<std::string, UniValue> contracts;

        // Load symbol tables if present
        if (root.exists("basic") && root["basic"].isObject()) {
            const std::vector<std::string>& keys = root["basic"].getKeys();
            for (const auto& k : keys) basicBlocks[k] = root["basic"][k];
        }
        if (root.exists("condition") && root["condition"].isObject()) {
            const std::vector<std::string>& keys = root["condition"].getKeys();
            for (const auto& k : keys) conditions[k] = root["condition"][k];
        }
        if (root.exists("contract") && root["contract"].isObject()) {
            const std::vector<std::string>& keys = root["contract"].getKeys();
            for (const auto& k : keys) contracts[k] = root["contract"][k];
        }

        // Determine the contract object to compile
        UniValue targetContract;
        if (root.exists("type") && root["type"].isStr() && root["type"].get_str() == "contract") {
            targetContract = root;
        } else {
            std::string active = "";
            if (root.exists("active_contract") && root["active_contract"].isStr()) {
                active = root["active_contract"].get_str();
            } else if (!contracts.empty()) {
                active = contracts.begin()->first;
            }

            if (active.empty() || contracts.find(active) == contracts.end()) {
                errorStr = "No active contract specified or found";
                return CScript();
            }
            targetContract = contracts[active];
        }

        CScript script;
        UniValue actions = targetContract["actions"];
        if (!actions.isArray() || actions.empty()) {
            errorStr = "Contract actions must be a non-empty array";
            return CScript();
        }

        size_t nSteps = 0;
        for (unsigned int i = 0; i < actions.size(); i++) {
            CScript actionScript = CompileAction(actions[i], basicBlocks, conditions, errorStr, 0, nSteps, false);
            if (!errorStr.empty()) return CScript();
            script += actionScript;
            if (script.size() > MAX_SCRIPT_SIZE) {
                errorStr = "Compiled MESCAL script exceeds MAX_SCRIPT_SIZE";
                return CScript();
            }
        }

        return script;
    } catch (const std::exception& e) {
        errorStr = std::string("MESCAL compilation error: ") + e.what();
        return CScript();
    }
}

UniValue CMescal::Decompile(const CScript& script, std::string& errorStr, int depth) {
    if (script.size() > MAX_SCRIPT_SIZE) {
        errorStr = "Script exceeds MAX_SCRIPT_SIZE";
        return UniValue();
    }
    if (depth > MAX_MESCAL_CONDITION_DEPTH) {
        errorStr = "Maximum condition nesting depth exceeded (max 32)";
        return UniValue();
    }
    try {
    UniValue root(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;

    struct DecompileState {
        std::vector<UniValue> stack;
    };

    DecompileState state;
    int basicCount = 0;
    int condCount = 0;

    // Helper to peek/match script patterns
    auto MatchPattern = [&](CScript::const_iterator& temp_pc, const std::vector<opcodetype>& pattern, std::vector<std::vector<unsigned char>>& pushedData) -> bool {
        pushedData.clear();
        for (opcodetype expected : pattern) {
            opcodetype op;
            std::vector<unsigned char> data;
            if (!script.GetOp(temp_pc, op, data)) return false;
            if (expected == OP_PUSHDATA4) {
                // Wildcard for data push
                if (!(op >= 0 && op <= OP_PUSHDATA4)) return false;
                pushedData.push_back(data);
            } else if (op != expected) {
                return false;
            }
        }
        return true;
    };

    while (pc < script.end()) {
        CScript::const_iterator next_pc = pc;
        std::vector<std::vector<unsigned char>> pushes;

        // Pattern: Coin Lock Miner Registration
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_DROP, OP_PUSHDATA4, OP_CHECKLOCKTIMEVERIFY, OP_DROP, OP_DUP, OP_HASH160, OP_PUSHDATA4, OP_EQUALVERIFY, OP_CHECKSIG}, pushes) && IsPubKey(pushes[0])) {
            pc = next_pc;
            CScriptNum lockTimeVal(pushes[1], true, 5);
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "coin-lock-miner-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "coin-lock-miner"));
            UniValue inputs(UniValue::VARR);
            
            UniValue inPubkey(UniValue::VOBJ);
            inPubkey.push_back(Pair("type", "pubkey"));
            inPubkey.push_back(Pair("name", "Pubkey"));
            inPubkey.push_back(Pair("value", HexStr(pushes[0])));
            inputs.push_back(inPubkey);

            UniValue inLockTime(UniValue::VOBJ);
            inLockTime.push_back(Pair("type", "timestamp-or-block-height"));
            inLockTime.push_back(Pair("name", "Lock-Until"));
            inLockTime.push_back(Pair("value", lockTimeVal.getint64()));
            inputs.push_back(inLockTime);

            UniValue inHash(UniValue::VOBJ);
            inHash.push_back(Pair("type", "pubkeyhash"));
            inHash.push_back(Pair("name", "PubkeyHash"));
            inHash.push_back(Pair("value", HexStr(pushes[2])));
            inputs.push_back(inHash);

            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern: PoW Miner Registration
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_PUSHDATA4, OP_PUSHDATA4, OP_DROP, OP_DROP, OP_DROP, OP_PUSHDATA4, OP_CHECKLOCKTIMEVERIFY, OP_DROP, OP_DUP, OP_HASH160, OP_PUSHDATA4, OP_EQUALVERIFY, OP_CHECKSIG}, pushes) && IsPubKey(pushes[2])) {
            pc = next_pc;
            CScriptNum lockTimeVal(pushes[3], true, 5);
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "pow-miner-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "pow-miner"));
            UniValue inputs(UniValue::VARR);
            
            UniValue inNonce(UniValue::VOBJ);
            inNonce.push_back(Pair("type", "nonce"));
            inNonce.push_back(Pair("name", "Nonce"));
            inNonce.push_back(Pair("value", HexStr(pushes[0])));
            inputs.push_back(inNonce);

            UniValue inChallenge(UniValue::VOBJ);
            inChallenge.push_back(Pair("type", "challenge"));
            inChallenge.push_back(Pair("name", "Challenge"));
            inChallenge.push_back(Pair("value", HexStr(pushes[1])));
            inputs.push_back(inChallenge);

            UniValue inPubkey(UniValue::VOBJ);
            inPubkey.push_back(Pair("type", "pubkey"));
            inPubkey.push_back(Pair("name", "Pubkey"));
            inPubkey.push_back(Pair("value", HexStr(pushes[2])));
            inputs.push_back(inPubkey);

            UniValue inLockTime(UniValue::VOBJ);
            inLockTime.push_back(Pair("type", "timestamp-or-block-height"));
            inLockTime.push_back(Pair("name", "Lock-Until"));
            inLockTime.push_back(Pair("value", lockTimeVal.getint64()));
            inputs.push_back(inLockTime);

            UniValue inHash(UniValue::VOBJ);
            inHash.push_back(Pair("type", "pubkeyhash"));
            inHash.push_back(Pair("name", "PubkeyHash"));
            inHash.push_back(Pair("value", HexStr(pushes[4])));
            inputs.push_back(inHash);

            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern: Multi-Signature (<m> <pubkey_1> ... <pubkey_n> <n> OP_CHECKMULTISIG)
        next_pc = pc;
        {
            auto DecodeScriptInt = [](opcodetype op, const std::vector<unsigned char>& data, int& outVal) -> bool {
                if (op == OP_0) { outVal = 0; return true; }
                if (op >= OP_1 && op <= OP_16) { outVal = CScript::DecodeOP_N(op); return true; }
                if (op > 0 && op <= OP_PUSHDATA4 && !data.empty() && data.size() <= 4) {
                    try {
                        outVal = CScriptNum(data, true).getint();
                        return true;
                    } catch (...) { return false; }
                }
                return false;
            };

            opcodetype opM;
            std::vector<unsigned char> dataM;
            int mVal = 0;
            if (script.GetOp(next_pc, opM, dataM) && DecodeScriptInt(opM, dataM, mVal) && mVal > 0 && mVal <= 20) {
                std::vector<std::string> pubkeyHexes;
                opcodetype opNext = OP_INVALIDOPCODE;
                std::vector<unsigned char> dataNext;
                bool hasNext = false;
                while (script.GetOp(next_pc, opNext, dataNext)) {
                    if (opNext > 0 && opNext <= OP_PUSHDATA4 && IsPubKey(dataNext)) {
                        pubkeyHexes.push_back(HexStr(dataNext.begin(), dataNext.end()));
                    } else {
                        hasNext = true;
                        break;
                    }
                }
                int nVal = 0;
                opcodetype opCheck = OP_INVALIDOPCODE;
                std::vector<unsigned char> dataCheck;
                if (hasNext && !pubkeyHexes.empty() &&
                    DecodeScriptInt(opNext, dataNext, nVal) &&
                    nVal == (int)pubkeyHexes.size() && mVal <= nVal &&
                    script.GetOp(next_pc, opCheck, dataCheck) && opCheck == OP_CHECKMULTISIG) {
                    pc = next_pc;
                    UniValue block(UniValue::VOBJ);
                    block.push_back(Pair("type", "basic"));
                    block.push_back(Pair("name", "multi-sig-" + std::to_string(basicCount++)));
                    block.push_back(Pair("role", "multi-signature"));
                    UniValue inputs(UniValue::VARR);

                    UniValue inM(UniValue::VOBJ);
                    inM.push_back(Pair("name", "m"));
                    inM.push_back(Pair("type", "number"));
                    inM.push_back(Pair("value", mVal));
                    inputs.push_back(inM);

                    UniValue inN(UniValue::VOBJ);
                    inN.push_back(Pair("name", "n"));
                    inN.push_back(Pair("type", "number"));
                    inN.push_back(Pair("value", nVal));
                    inputs.push_back(inN);

                    UniValue sigsArr(UniValue::VARR);
                    for (const auto& pkHex : pubkeyHexes) {
                        sigsArr.push_back(pkHex);
                    }
                    UniValue inSigs(UniValue::VOBJ);
                    inSigs.push_back(Pair("name", "Signatures"));
                    inSigs.push_back(Pair("type", "array"));
                    inSigs.push_back(Pair("value", sigsArr));
                    inputs.push_back(inSigs);

                    block.push_back(Pair("inputs", inputs));
                    state.stack.push_back(block);
                    continue;
                }
            }
        }

        // Pattern 1: lock-time (<expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_CHECKLOCKTIMEVERIFY, OP_DROP}, pushes)) {
            pc = next_pc; // Consume matched instructions
            CScriptNum val(pushes[0], true, 5);
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "lock-time-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "lock-time"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "timestamp-or-block-height"));
            input.push_back(Pair("name", "Lock-Until"));
            input.push_back(Pair("value", val.getint64()));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 2: equalverify-checksig (OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_DUP, OP_HASH160, OP_PUSHDATA4, OP_EQUALVERIFY, OP_CHECKSIG}, pushes)) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "equalverify-checksig-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "equalverify-checksig"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "pubkeyhash"));
            input.push_back(Pair("name", "PubkeyHash"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 3: check-signature-verification (<pubkey> OP_CHECKSIGVERIFY veya <pubkey> OP_CHECKSIG)
        next_pc = pc;
        bool isCheckSigVerify = MatchPattern(next_pc, {OP_PUSHDATA4, OP_CHECKSIGVERIFY}, pushes) && IsPubKey(pushes[0]);
        if (!isCheckSigVerify) {
            next_pc = pc;
            if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_CHECKSIG}, pushes) && IsPubKey(pushes[0])) {
                pc = next_pc;
                UniValue block(UniValue::VOBJ);
                block.push_back(Pair("type", "basic"));
                block.push_back(Pair("name", "check-sig-" + std::to_string(basicCount++)));
                block.push_back(Pair("role", "check-signature-verification"));
                block.push_back(Pair("verify", false));
                UniValue inputs(UniValue::VARR);
                UniValue input(UniValue::VOBJ);
                input.push_back(Pair("type", "pubkey"));
                input.push_back(Pair("name", "Pubkey"));
                input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
                inputs.push_back(input);
                block.push_back(Pair("inputs", inputs));
                state.stack.push_back(block);
                continue;
            }
        } else {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "check-sig-verify-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "check-signature-verification"));
            block.push_back(Pair("verify", true));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "pubkey"));
            input.push_back(Pair("name", "Pubkey"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 4a: HTLC hash160 (OP_HASH160 <20-byte-hash> OP_EQUAL)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_HASH160, OP_PUSHDATA4, OP_EQUAL}, pushes) && pushes[0].size() == 20) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "hash160-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "hash160"));
            block.push_back(Pair("mode", "htlc-equal"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "hash160"));
            input.push_back(Pair("name", "hash160"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 4b: Standalone Hash-Lock (OP_HASH160 <20-byte-hash> OP_EQUALVERIFY)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_HASH160, OP_PUSHDATA4, OP_EQUALVERIFY}, pushes) && pushes[0].size() == 20) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "hash160-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "hash160"));
            block.push_back(Pair("mode", "hash-equalverify"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "hash160"));
            input.push_back(Pair("name", "hash160"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Pattern 4: hash160 (<val> OP_HASH160)
        next_pc = pc;
        if (MatchPattern(next_pc, {OP_PUSHDATA4, OP_HASH160}, pushes)) {
            pc = next_pc;
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "hash160-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "hash160"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "string-or-number"));
            input.push_back(Pair("name", "String-or-Number-to-Hash"));
            input.push_back(Pair("value", HexStr(pushes[0].begin(), pushes[0].end())));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
            continue;
        }

        // Parse generic opcode or push
        if (!script.GetOp(pc, opcode, vch)) {
            errorStr = "Malformed script";
            return UniValue();
        }

        if (opcode >= 0 && opcode <= OP_PUSHDATA4) {
            // Push number
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "number-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "number"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "number"));
            input.push_back(Pair("name", "Number"));
            try {
                CScriptNum num(vch, true);
                input.push_back(Pair("value", num.getint64()));
            } catch (...) {
                errorStr = "Invalid number push in MESCAL contract";
                return UniValue();
            }
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
        } else if (opcode == OP_1NEGATE || (opcode >= OP_1 && opcode <= OP_16)) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "number-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "number"));
            UniValue inputs(UniValue::VARR);
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type", "number"));
            input.push_back(Pair("name", "Number"));
            input.push_back(Pair("value", opcode == OP_1NEGATE ? (int64_t)-1 : (int64_t)CScript::DecodeOP_N(opcode)));
            inputs.push_back(input);
            block.push_back(Pair("inputs", inputs));
            state.stack.push_back(block);
        } else if (opcode == OP_ADD) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "add-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "add"));
            state.stack.push_back(block);
        } else if (opcode == OP_SUB) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "sub-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "sub"));
            state.stack.push_back(block);
        } else if (opcode == OP_BOOLAND) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "bool-and-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "bool-and"));
            state.stack.push_back(block);
        } else if (opcode == OP_BOOLOR) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "bool-or-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "bool-or"));
            state.stack.push_back(block);
        } else if (opcode == OP_NUMEQUAL) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "num-equal-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "num-equal"));
            state.stack.push_back(block);
        } else if (opcode == OP_DROP) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "drop-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "drop"));
            state.stack.push_back(block);
        } else if (opcode == OP_GREATERTHAN) {
            UniValue block(UniValue::VOBJ);
            block.push_back(Pair("type", "basic"));
            block.push_back(Pair("name", "greater-than-" + std::to_string(basicCount++)));
            block.push_back(Pair("role", "greater-than"));
            state.stack.push_back(block);
        } else if (opcode == OP_IF) {
            // Collect expression(s) preceding OP_IF (including any OP_BOOLAND-chained expressions)
            std::vector<UniValue> exprList;
            if (!state.stack.empty()) {
                exprList.push_back(state.stack.back());
                state.stack.pop_back();
                while (state.stack.size() >= 2 &&
                       exprList.back().isObject() &&
                       exprList.back().exists("role") &&
                       exprList.back()["role"].get_str() == "bool-and") {
                    exprList.pop_back(); // remove bool-and marker
                    exprList.push_back(state.stack.back());
                    state.stack.pop_back();
                    if (!state.stack.empty() &&
                        state.stack.back().isObject() &&
                        state.stack.back().exists("role") &&
                        state.stack.back()["role"].get_str() != "bool-and") {
                        exprList.push_back(state.stack.back());
                        state.stack.pop_back();
                    }
                }
                std::reverse(exprList.begin(), exprList.end());
            } else {
                UniValue expr(UniValue::VOBJ);
                expr.push_back(Pair("type", "basic"));
                expr.push_back(Pair("name", "stack-input-" + std::to_string(basicCount++)));
                expr.push_back(Pair("role", "stack-input"));
                exprList.push_back(expr);
            }

            // Find matching ELSE or ENDIF
            CScript trueScript;
            CScript falseScript;
            bool inFalse = false;
            int ifDepth = 1;

            while (pc < script.end()) {
                opcodetype innerOp;
                std::vector<unsigned char> innerVch;
                CScript::const_iterator mark = pc;
                if (!script.GetOp(pc, innerOp, innerVch)) break;

                if (innerOp == OP_NOTIF) {
                    errorStr = "OP_NOTIF is not supported in MESCAL contracts";
                    return UniValue();
                } else if (innerOp == OP_IF) {
                    ifDepth++;
                    if (depth + ifDepth > MAX_MESCAL_CONDITION_DEPTH) {
                        errorStr = "Maximum condition nesting depth exceeded (max 32)";
                        return UniValue();
                    }
                } else if (innerOp == OP_ENDIF) {
                    ifDepth--;
                    if (ifDepth == 0) break;
                } else if (innerOp == OP_ELSE && ifDepth == 1) {
                    if (inFalse) {
                        errorStr = "Duplicate OP_ELSE in MESCAL contract";
                        return UniValue();
                    }
                    inFalse = true;
                    continue;
                }

                // Append the exact raw instruction bytes [mark, pc)
                if (inFalse) {
                    falseScript.insert(falseScript.end(), mark, pc);
                } else {
                    trueScript.insert(trueScript.end(), mark, pc);
                }
            }

            if (ifDepth != 0) {
                errorStr = "Unclosed OP_IF in MESCAL contract";
                return UniValue();
            }

            UniValue condBlock(UniValue::VOBJ);
            condBlock.push_back(Pair("type", "condition"));
            condBlock.push_back(Pair("name", "if-condition-" + std::to_string(condCount++)));
            condBlock.push_back(Pair("role", "if-condition"));

            UniValue exprs(UniValue::VARR);
            for (const auto& e : exprList) {
                exprs.push_back(e);
            }
            condBlock.push_back(Pair("expressions", exprs));

            std::string err;
            UniValue trueDecomp = CMescal::Decompile(trueScript, err, depth + 1);
            if (!err.empty()) {
                errorStr = err;
                return UniValue();
            }
            condBlock.push_back(Pair("true", trueDecomp.exists("actions") ? trueDecomp["actions"] : UniValue(UniValue::VARR)));
            if (falseScript.size() > 0) {
                UniValue falseDecomp = CMescal::Decompile(falseScript, err, depth + 1);
                if (!err.empty()) {
                    errorStr = err;
                    return UniValue();
                }
                condBlock.push_back(Pair("false", falseDecomp.exists("actions") ? falseDecomp["actions"] : UniValue(UniValue::VARR)));
            }
            state.stack.push_back(condBlock);
        } else {
            errorStr = "Unsupported opcode in MESCAL contract";
            return UniValue();
        }
    }

    for (const auto& item : state.stack) {
        actions.push_back(item);
    }

    root.push_back(Pair("type", "contract"));
    root.push_back(Pair("name", "Decompiled-Contract"));
    root.push_back(Pair("actions", actions));
    return root;
    } catch (const std::exception& e) {
        errorStr = std::string("MESCAL decompilation error: ") + e.what();
        return UniValue();
    }
}
