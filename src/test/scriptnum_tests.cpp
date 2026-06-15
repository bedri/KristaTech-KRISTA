// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/script.h"
#include "test/test_kristatech.h"

#include <boost/test/unit_test.hpp>
#include <limits.h>
#include <stdint.h>
#include <vector>

class CBigNum {
private:
    __int128_t val;

public:
    CBigNum() : val(0) {}
    CBigNum(int64_t n) : val(n) {}

    CBigNum(const std::vector<unsigned char>& vch) {
        if (vch.empty()) {
            val = 0;
            return;
        }
        __int128_t tmp = 0;
        for (size_t i = 0; i < vch.size(); ++i) {
            tmp |= (__int128_t(vch[i]) << (8 * i));
        }
        if (vch.back() & 0x80) {
            tmp &= ~(__int128_t(0x80) << (8 * (vch.size() - 1)));
            val = -tmp;
        } else {
            val = tmp;
        }
    }

    std::vector<unsigned char> getvch() const {
        std::vector<unsigned char> vch;
        if (val == 0) return vch;
        __int128_t tmp = (val < 0) ? -val : val;
        while (tmp > 0) {
            vch.push_back((unsigned char)(tmp & 0xFF));
            tmp >>= 8;
        }
        if (vch.back() & 0x80) {
            vch.push_back((val < 0) ? 0x80 : 0);
        } else if (val < 0) {
            vch.back() |= 0x80;
        }
        return vch;
    }

    int getint() const {
        if (val > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        else if (val < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return (int)val;
    }

    friend bool operator==(const CBigNum& a, const CBigNum& b) { return a.val == b.val; }
    friend bool operator!=(const CBigNum& a, const CBigNum& b) { return a.val != b.val; }
    friend bool operator<(const CBigNum& a, const CBigNum& b) { return a.val < b.val; }
    friend bool operator>(const CBigNum& a, const CBigNum& b) { return a.val > b.val; }
    friend bool operator<=(const CBigNum& a, const CBigNum& b) { return a.val <= b.val; }
    friend bool operator>=(const CBigNum& a, const CBigNum& b) { return a.val >= b.val; }

    CBigNum operator-() const {
        CBigNum ret;
        ret.val = -val;
        return ret;
    }

    friend CBigNum operator+(const CBigNum& a, const CBigNum& b) {
        CBigNum ret;
        ret.val = a.val + b.val;
        return ret;
    }

    friend CBigNum operator-(const CBigNum& a, const CBigNum& b) {
        CBigNum ret;
        ret.val = a.val - b.val;
        return ret;
    }
};

BOOST_FIXTURE_TEST_SUITE(scriptnum_tests, BasicTestingSetup)

static const long values[] = \
{ 0, 1, CHAR_MIN, CHAR_MAX, UCHAR_MAX, SHRT_MIN, USHRT_MAX, INT_MIN, INT_MAX, static_cast<long>UINT_MAX, LONG_MIN, LONG_MAX };
static const long offsets[] = { 1, 0x79, 0x80, 0x81, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000};

static bool verify(const CBigNum& bignum, const CScriptNum& scriptnum)
{
    return bignum.getvch() == scriptnum.getvch() && bignum.getint() == scriptnum.getint();
}

static void CheckCreateVch(const long& num)
{
    CBigNum bignum(num);
    CScriptNum scriptnum(num);
    BOOST_CHECK(verify(bignum, scriptnum));

    CBigNum bignum2(bignum.getvch());
    CScriptNum scriptnum2(scriptnum.getvch(), false);
    BOOST_CHECK(verify(bignum2, scriptnum2));

    CBigNum bignum3(scriptnum2.getvch());
    CScriptNum scriptnum3(bignum2.getvch(), false);
    BOOST_CHECK(verify(bignum3, scriptnum3));
}

static void CheckCreateInt(const long& num)
{
    CBigNum bignum(num);
    CScriptNum scriptnum(num);
    BOOST_CHECK(verify(bignum, scriptnum));
    BOOST_CHECK(verify(bignum.getint(), CScriptNum(scriptnum.getint())));
    BOOST_CHECK(verify(scriptnum.getint(), CScriptNum(bignum.getint())));
    BOOST_CHECK(verify(CBigNum(scriptnum.getint()).getint(), CScriptNum(CScriptNum(bignum.getint()).getint())));
}


static void CheckAdd(const long& num1, const long& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    CBigNum bignum3(num1);
    CBigNum bignum4(num1);
    CScriptNum scriptnum3(num1);
    CScriptNum scriptnum4(num1);

    // int64_t overflow is undefined.
    bool invalid = (((num2 > 0) && (num1 > (std::numeric_limits<long>::max() - num2))) ||
                    ((num2 < 0) && (num1 < (std::numeric_limits<long>::min() - num2))));
    if (!invalid)
    {
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum1 + scriptnum2));
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum1 + num2));
        BOOST_CHECK(verify(bignum1 + bignum2, scriptnum2 + num1));
    }
}

static void CheckNegate(const long& num)
{
    const CBigNum bignum(num);
    const CScriptNum scriptnum(num);

    // -INT64_MIN is undefined
    if (num != std::numeric_limits<long>::min())
        BOOST_CHECK(verify(-bignum, -scriptnum));
}

static void CheckSubtract(const long& num1, const long& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    bool invalid = false;

    // int64_t overflow is undefined.
    invalid = ((num2 > 0 && num1 < std::numeric_limits<long>::min() + num2) ||
               (num2 < 0 && num1 > std::numeric_limits<long>::max() + num2));
    if (!invalid)
    {
        BOOST_CHECK(verify(bignum1 - bignum2, scriptnum1 - scriptnum2));
        BOOST_CHECK(verify(bignum1 - bignum2, scriptnum1 - num2));
    }

    invalid = ((num1 > 0 && num2 < std::numeric_limits<long>::min() + num1) ||
               (num1 < 0 && num2 > std::numeric_limits<long>::max() + num1));
    if (!invalid)
    {
        BOOST_CHECK(verify(bignum2 - bignum1, scriptnum2 - scriptnum1));
        BOOST_CHECK(verify(bignum2 - bignum1, scriptnum2 - num1));
    }
}

static void CheckCompare(const long& num1, const long& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);

    BOOST_CHECK((bignum1 == bignum1) == (scriptnum1 == scriptnum1));
    BOOST_CHECK((bignum1 != bignum1) ==  (scriptnum1 != scriptnum1));
    BOOST_CHECK((bignum1 < bignum1) ==  (scriptnum1 < scriptnum1));
    BOOST_CHECK((bignum1 > bignum1) ==  (scriptnum1 > scriptnum1));
    BOOST_CHECK((bignum1 >= bignum1) ==  (scriptnum1 >= scriptnum1));
    BOOST_CHECK((bignum1 <= bignum1) ==  (scriptnum1 <= scriptnum1));

    BOOST_CHECK((bignum1 == bignum1) == (scriptnum1 == num1));
    BOOST_CHECK((bignum1 != bignum1) ==  (scriptnum1 != num1));
    BOOST_CHECK((bignum1 < bignum1) ==  (scriptnum1 < num1));
    BOOST_CHECK((bignum1 > bignum1) ==  (scriptnum1 > num1));
    BOOST_CHECK((bignum1 >= bignum1) ==  (scriptnum1 >= num1));
    BOOST_CHECK((bignum1 <= bignum1) ==  (scriptnum1 <= num1));

    BOOST_CHECK((bignum1 == bignum2) ==  (scriptnum1 == scriptnum2));
    BOOST_CHECK((bignum1 != bignum2) ==  (scriptnum1 != scriptnum2));
    BOOST_CHECK((bignum1 < bignum2) ==  (scriptnum1 < scriptnum2));
    BOOST_CHECK((bignum1 > bignum2) ==  (scriptnum1 > scriptnum2));
    BOOST_CHECK((bignum1 >= bignum2) ==  (scriptnum1 >= scriptnum2));
    BOOST_CHECK((bignum1 <= bignum2) ==  (scriptnum1 <= scriptnum2));

    BOOST_CHECK((bignum1 == bignum2) ==  (scriptnum1 == num2));
    BOOST_CHECK((bignum1 != bignum2) ==  (scriptnum1 != num2));
    BOOST_CHECK((bignum1 < bignum2) ==  (scriptnum1 < num2));
    BOOST_CHECK((bignum1 > bignum2) ==  (scriptnum1 > num2));
    BOOST_CHECK((bignum1 >= bignum2) ==  (scriptnum1 >= num2));
    BOOST_CHECK((bignum1 <= bignum2) ==  (scriptnum1 <= num2));
}

static void RunCreate(const long& num)
{
    CheckCreateInt(num);
    CScriptNum scriptnum(num);
    if (scriptnum.getvch().size() <= CScriptNum::nDefaultMaxNumSize)
        CheckCreateVch(num);
    else
    {
        BOOST_CHECK_THROW (CheckCreateVch(num), scriptnum_error);
    }
}

static void RunOperators(const long& num1, const int64_t& num2)
{
    CheckAdd(num1, num2);
    CheckSubtract(num1, num2);
    CheckNegate(num1);
    CheckCompare(num1, num2);
}

BOOST_AUTO_TEST_CASE(creation)
{
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j)
        {
            RunCreate(values[i]);
            RunCreate(static_cast<long>(static_cast<uint64_t>(values[i]) + offsets[j]));
            RunCreate(static_cast<long>(static_cast<uint64_t>(values[i]) - offsets[j]));
        }
    }
}

BOOST_AUTO_TEST_CASE(operators)
{
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j)
        {
            RunOperators(values[i], values[i]);
            RunOperators(values[i], static_cast<long>(-static_cast<uint64_t>(values[i])));
            RunOperators(values[i], values[j]);
            RunOperators(values[i], static_cast<long>(-static_cast<uint64_t>(values[j])));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]), values[j]);
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]), static_cast<long>(-static_cast<uint64_t>(values[j])));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]), values[j]);
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]), static_cast<long>(-static_cast<uint64_t>(values[j])));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]), static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]), static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]), static_cast<long>(static_cast<uint64_t>(values[i]) + values[j]));
            RunOperators(static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]), static_cast<long>(static_cast<uint64_t>(values[i]) - values[j]));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
