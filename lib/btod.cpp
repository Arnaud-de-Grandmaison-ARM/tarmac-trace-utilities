/*
 * Copyright 2016-2021 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of Tarmac Trace Utilities
 */

#include <cassert>
#include <map>
#include <string>
#include <vector>

using std::map;
using std::string;
using std::vector;

class Bigint {
    vector<unsigned char> digits; // LSB-first, values 0..9 only
    void normalise(size_t i = 0)
    {
        int carry = 0;
        for (; i < digits.size(); ++i) {
            carry += digits[i];
            digits[i] = carry % 10;
            carry /= 10;
        }
        assert(!carry);
    }
    void contract()
    {
        size_t size = digits.size();
        while (size > 0 && digits[size - 1] == 0)
            size--;
        digits.resize(size);
    }
    void expand(size_t size)
    {
        if (size > digits.size())
            digits.resize(size, 0);
    }

  public:
    Bigint() {}
    Bigint(unsigned long long val, size_t extradigits = 0, int filldigit = 0)
    {
        char buf[32];
        digits.resize(extradigits, filldigit);
        size_t i = sprintf(buf, "%.0llu", val); // generate no digits for zero
        while (i-- > 0)
            digits.push_back(buf[i] - '0');
    }
    void operator+=(const Bigint &rhs)
    {
        expand(rhs.digits.size() + 1);
        for (size_t i = 0; i < rhs.digits.size(); ++i)
            digits[i] += rhs.digits[i];
        normalise();
        contract();
    }
    void operator*=(const Bigint &rhs)
    {
        size_t oldsize = digits.size();
        expand(digits.size() + rhs.digits.size() + 1);
        for (size_t i = oldsize; i-- > 0;) {
            int digit = digits[i];
            digits[i] = 0;
            for (size_t j = 0; j < rhs.digits.size(); ++j)
                digits[i + j] += rhs.digits[j] * digit;
            normalise(i);
        }
        contract();
    }
    int ndigits() const { return digits.size(); }
    int digit(int i) const
    {
        return i >= 0 && (unsigned)i < digits.size() ? digits[i] : 0;
    }
};

static map<unsigned, Bigint> powers2, powers5;

static Bigint power_of(int v, unsigned n)
{
    assert(v == 2 || v == 5);
    auto &powers = (v == 2 ? powers2 : powers5);
    if (powers.find(n) == powers.end()) {
        unsigned lowbit = n & -n;
        if (n != lowbit) {
            Bigint a = power_of(v, n - lowbit);
            Bigint b = power_of(v, lowbit);
            a *= b;
            powers[n] = a;
        } else if (n > 1) {
            Bigint a = power_of(v, n / 2);
            Bigint b = a;
            a *= b;
            powers[n] = a;
        } else {
            powers[n] = Bigint(n == 1 ? v : 1);
        }
    }
    return powers[n];
}

// Return a decimal representation of mantissa * 2^power2, to the
// given number of significant figures.
static string fp_btod(unsigned long long mantissa, int power2, int precision)
{
    Bigint val(mantissa);
    int power10 = 0;
    if (power2 > 0) {
        val *= power_of(2, power2);
    } else if (power2 < 0) {
        val *= power_of(5, -power2);
        power10 += power2;
    }
    string ret;
    int digitpos = val.ndigits() - 1;
    power10 += digitpos;

    // Round to nearest at the (digitpos-precision)th significant
    // figure.
    //
    // If we just wanted to break ties by rounding up, we could simply
    // add 5*10^(n-1) and that would push all the digits we actually
    // output into the right values. But in fact we want to round to
    // nearest with ties broken by rounding to even. This means that
    // if the least significant digit we _are_ outputting is odd, then
    // we do that, and otherwise we want to break ties by rounding
    // down, for which it's sufficient to subtract 1 from the value we
    // added.
    if (digitpos - precision >= 0) {
        if (val.digit(digitpos - precision) >= 5) {
            if (val.digit(digitpos - precision + 1) & 1)
                val += Bigint(5, digitpos - precision, 0);
            else
                val += Bigint(4, digitpos - precision, 9);
        }
    }

    ret.push_back('0' + val.digit(digitpos--));
    ret.push_back('.');
    for (int i = 1; i < precision; i++)
        ret.push_back('0' + val.digit(digitpos--));
    {
        char expstr[32];
        sprintf(expstr, "e%+03d", val.ndigits() == 0 ? 0 : power10);
        ret += expstr;
    }
    return ret;
}

string ieee_btod(unsigned long long val, int ebits, int mbits, int digits)
{
    string ret = (val & (1ULL << (ebits + mbits)) ? "-" : " ");
    int exp = (val >> mbits) & ((1ULL << ebits) - 1);
    val &= (1ULL << mbits) - 1;
    if (exp == (int)((1ULL << ebits) - 1)) {
        ret += val ? "NaN" : "Inf";
        return ret;
    }
    if (exp) {
        val |= (1ULL << mbits);
        exp--;
    }
    exp -= (1ULL << (ebits - 1)) - 2 + mbits;
    ret += fp_btod(val, exp, digits);
    return ret;
}

string float_btod(unsigned val) { return ieee_btod(val, 8, 23, 9); }
string double_btod(unsigned long long val)
{
    return ieee_btod(val, 11, 52, 17);
}

#ifdef TEST_BTOD

#include <iostream>

#define TEST(fncall, expected)                                                 \
    do {                                                                       \
        string got = fncall;                                                   \
        if (got == expected) {                                                 \
            pass++;                                                            \
        } else {                                                               \
            clog << "line " << __LINE__ << ": " << #fncall << " returned \""   \
                 << got << "\", expected \"" << expected << "\"" << endl;      \
            fail++;                                                            \
        }                                                                      \
    } while (0)

int main(void)
{
    int pass = 0, fail = 0;
    TEST(float_btod(0x7f800001), " NaN");
    TEST(float_btod(0x7f800000), " Inf");
    TEST(float_btod(0x7f7fffff), " 3.40282347e+38");
    TEST(float_btod(0x00800000), " 1.17549435e-38");
    TEST(float_btod(0x807fffff), "-1.17549421e-38");
    TEST(float_btod(0x00000001), " 1.40129846e-45");
    TEST(float_btod(0x00000000), " 0.00000000e+00");
    TEST(float_btod(0x3f804000), " 1.00195312e+00");
    TEST(float_btod(0x3f80c000), " 1.00585938e+00");
    TEST(float_btod(0x3f800000), " 1.00000000e+00");
    TEST(float_btod(0x3f800001), " 1.00000012e+00");
    TEST(float_btod(0x3f7fffff), " 9.99999940e-01");
    TEST(float_btod(0x40490fdb), " 3.14159274e+00");
    TEST(double_btod(0x7ff0000000000001ULL), " NaN");
    TEST(double_btod(0x7ff0000000000000ULL), " Inf");
    TEST(double_btod(0x7fefffffffffffffULL), " 1.7976931348623157e+308");
    TEST(double_btod(0x0010000000000000ULL), " 2.2250738585072014e-308");
    TEST(double_btod(0x800fffffffffffffULL), "-2.2250738585072009e-308");
    TEST(double_btod(0x0000000000000001ULL), " 4.9406564584124654e-324");
    TEST(double_btod(0x0000000000000000ULL), " 0.0000000000000000e+00");
    TEST(double_btod(0x3ff0000800000000ULL), " 1.0000076293945312e+00");
    TEST(double_btod(0x3ff0001800000000ULL), " 1.0000228881835938e+00");
    TEST(double_btod(0x3ff0000000000000ULL), " 1.0000000000000000e+00");
    TEST(double_btod(0x3ff0000000000001ULL), " 1.0000000000000002e+00");
    TEST(double_btod(0x3fefffffffffffffULL), " 9.9999999999999989e-01");
    TEST(double_btod(0x400921fb54442d18ULL), " 3.1415926535897931e+00");
    cout << "pass " << pass << " fail " << fail << endl;
    return fail == 0;
}

#endif
