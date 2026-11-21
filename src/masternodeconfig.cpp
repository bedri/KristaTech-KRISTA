// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "netbase.h"
#include "masternodeconfig.h"
#include "util.h"
#include "guiinterface.h"
#include <base58.h>
#ifndef WIN32
#include <sys/stat.h>
#endif

static void RestrictConfigPermissions(const fs::path& filePath)
{
#ifndef WIN32
    chmod(filePath.string().c_str(), S_IRUSR | S_IWUSR);
#endif
}

CMasternodeConfig masternodeConfig;

void CMasternodeConfig::clear()
{
    entries.clear();
}

CMasternodeConfig::CMasternodeEntry* CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
    return &(entries[entries.size()-1]);
}

void CMasternodeConfig::remove(std::string alias) {
    int pos = -1;
    for (int i = 0; i < ((int) entries.size()); ++i) {
        CMasternodeEntry e = entries[i];
        if (e.getAlias() == alias) {
            pos = i;
            break;
        }
    }
    if (pos != -1) {
        entries.erase(entries.begin() + pos);
    }
}

bool CMasternodeConfig::read(std::string& strErr)
{
    int linenumber = 1;
    fs::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    fs::ifstream streamConfig(pathMasternodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fsbridge::fopen(pathMasternodeConfigFile, "a");
        if (configFile != NULL) {
            std::string strHeader = "# Masternode config file\n"
                                    "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:__PORT_MAINNET__ 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0"
                                    "#\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
            RestrictConfigPermissions(pathMasternodeConfigFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse masternode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        int port = 0;
        int nDefaultPort = Params().GetDefaultPort();
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if(port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n"+
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }

        if (port != nDefaultPort) {
            strErr = strprintf(_("Invalid port %d detected in masternode.conf"), port) + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + ip + "\"" + "\n" +
                     strprintf(_("(must be %d for %s-net)"), nDefaultPort, Params().NetworkIDString());
            streamConfig.close();
            return false;
        }


        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

bool CMasternodeConfig::CMasternodeEntry::castOutputIndex(int &n) const
{
    try {
        n = std::stoi(outputIndex);
    } catch (const std::exception& e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}

static std::string GetDefaultMasternodeConfHeader()
{
    const std::string defaultPortStr = std::to_string(Params().GetDefaultPort());
    return "# Masternode config file (Cold Controller Wallet)\n"
           "# Note: Remote Hot Masternodes use activemasternode.conf (<alias> <privkey>) for single-IP multi-MN;\n"
           "#       never place masternodeprivkey=... inside kristatech.conf on the remote server.\n"
           "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
           "# Example: mn1 127.0.0.2:" + defaultPortStr + " 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
}

static bool ReadValidatedMasternodeLines(std::string& lineCopyOut, std::string& strErr)
{
    fs::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    if (!fs::exists(pathMasternodeConfigFile)) {
        const std::string header = GetDefaultMasternodeConfHeader();
        FILE* initFile = fsbridge::fopen(pathMasternodeConfigFile, "w");
        if (!initFile) {
            strErr = "Cannot create masternode.conf in data directory.";
            return false;
        }
        fwrite(header.c_str(), header.size(), 1, initFile);
        fclose(initFile);
        RestrictConfigPermissions(pathMasternodeConfigFile);
    }

    fs::ifstream streamConfig(pathMasternodeConfigFile);
    if (!streamConfig.good()) {
        strErr = "Invalid or unreadable masternode.conf file.";
        return false;
    }

    int linenumber = 1;
    lineCopyOut.clear();
    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, existingAlias, ip, privKey, txHash, outputIndex;
        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }
        if (!(iss >> existingAlias >> ip >> privKey >> txHash >> outputIndex)) {
            streamConfig.close();
            strErr = strprintf("Error parsing masternode.conf at line %d", linenumber);
            return false;
        }
        lineCopyOut += line + "\n";
    }
    streamConfig.close();
    if (lineCopyOut.empty()) {
        lineCopyOut = GetDefaultMasternodeConfHeader();
    }
    return true;
}

bool CMasternodeConfig::validateWritable(std::string& strErr) const
{
    std::string unusedLines;
    return ReadValidatedMasternodeLines(unusedLines, strErr);
}

CMasternodeConfig::CMasternodeEntry* CMasternodeConfig::appendEntryAndSave(
    const std::string& alias,
    const std::string& ipPort,
    const std::string& privKey,
    const std::string& txHash,
    const std::string& outputIndex,
    std::string& strErr)
{
    std::string lineCopy;
    if (!ReadValidatedMasternodeLines(lineCopy, strErr)) {
        return nullptr;
    }
    lineCopy += "\n" + alias + " " + ipPort + " " + privKey + " " + txHash + " " + outputIndex + "\n";

    fs::path pathConfigFile = AbsPathForConfigVal(fs::path("masternode_temp.conf"));
    FILE* configFile = fsbridge::fopen(pathConfigFile, "w");
    if (!configFile) {
        strErr = "Failed to write temporary masternode_temp.conf file.";
        return nullptr;
    }
    fwrite(lineCopy.c_str(), lineCopy.size(), 1, configFile);
    fclose(configFile);
    RestrictConfigPermissions(pathConfigFile);

    fs::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    fs::path pathOldConfFile = AbsPathForConfigVal(fs::path("old_masternode.conf"));
    if (fs::exists(pathOldConfFile)) {
        fs::remove(pathOldConfFile);
    }
    rename(pathMasternodeConfigFile, pathOldConfFile);
    rename(pathConfigFile, pathMasternodeConfigFile);
    RestrictConfigPermissions(pathMasternodeConfigFile);
    if (fs::exists(pathOldConfFile)) {
        fs::remove(pathOldConfFile);
    }

    return add(alias, ipPort, privKey, txHash, outputIndex);
}
