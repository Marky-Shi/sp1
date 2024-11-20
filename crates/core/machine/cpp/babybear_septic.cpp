#include "babybear_septic.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
using namespace sp1_core_machine_sys;

BabyBearSeptic BabyBearSeptic::zero() {
    return BabyBearSeptic(BabyBear::zero());
}

BabyBearSeptic BabyBearSeptic::one() {
    return BabyBearSeptic(BabyBear::one());
}

BabyBearSeptic BabyBearSeptic::two() {
    return BabyBearSeptic(BabyBear::two());
}

BabyBearSeptic BabyBearSeptic::from_canonical_u32(uint32_t n) {
    return BabyBearSeptic(BabyBear::from_canonical_u32(n));
}

BabyBearSeptic& BabyBearSeptic::operator+=(const BabyBear b) {
    value[0] += b;
    return *this;
}

BabyBearSeptic& BabyBearSeptic::operator+=(const BabyBearSeptic b) {
    for (uintptr_t i = 0 ; i < 7 ; i++) {
        value[i] += b.value[i];
    }
    return *this;
}

BabyBearSeptic& BabyBearSeptic::operator-=(const BabyBear b) {
    value[0] -= b;
    return *this;
}

BabyBearSeptic& BabyBearSeptic::operator-=(const BabyBearSeptic b) {
    for (uintptr_t i = 0 ; i < 7 ; i++) {
        value[i] -= b.value[i];
    }
    return *this;
}

BabyBearSeptic& BabyBearSeptic::operator*=(const BabyBear b) {
    for (uintptr_t i = 0 ; i < 7 ; i++) {
        value[i] *= b;
    }
    return *this;
}

BabyBearSeptic& BabyBearSeptic::operator*=(const BabyBearSeptic b) {
    BabyBear res[13] = {};
    for(uintptr_t i = 0 ; i < 13 ; i++) {
        res[i] = BabyBear::zero();
    }
    for(uintptr_t i = 0 ; i < 7 ; i++) {
        for(uintptr_t j = 0 ; j < 7 ; j++) {
            res[i + j] += value[i] * b.value[j];
        }
    }
    for(uintptr_t i = 7 ; i < 13 ; i++) {
        res[i - 7] += res[i] * BabyBear::from_canonical_u32(5);
        res[i - 6] += res[i] * BabyBear::from_canonical_u32(2);
    }
    for(uintptr_t i = 0 ; i < 7 ; i++) {
        value[i] = res[i];
    }
    return *this;
}

bool BabyBearSeptic::operator==(const BabyBearSeptic rhs) const {
    for(uintptr_t i = 0 ; i < 7 ; i++) {
        if(value[i] != rhs.value[i]) {
            return false;
        }
    }
    return true;
}

BabyBearSeptic BabyBearSeptic::frobenius() const {
    BabyBear res[7] = {};
    res[0] = value[0];
    for(uintptr_t i = 1 ; i < 7 ; i++) {
        res[i] = BabyBear::zero();
    }
    for(uintptr_t i = 1 ; i < 7 ; i++) {
        for(uintptr_t j = 0 ; j < 7 ; j++) {
            res[j] += value[i] * frobenius_const[i][j];
        }
    }
    return BabyBearSeptic(res);
}

BabyBearSeptic BabyBearSeptic::double_frobenius() const {
    BabyBear res[7] = {};
    res[0] = value[0];
    for(uintptr_t i = 1 ; i < 7 ; i++) {
        res[i] = BabyBear::zero();
    }
    for(uintptr_t i = 1 ; i < 7 ; i++) {
        for(uintptr_t j = 0 ; j < 7 ; j++) {
            res[j] += value[i] * double_frobenius_const[i][j];
        }
    }
    return BabyBearSeptic(res);
}

BabyBearSeptic BabyBearSeptic::pow_r_1() const {
    BabyBearSeptic base = frobenius();
    base *= double_frobenius();
    BabyBearSeptic base_p2 = base.double_frobenius();
    BabyBearSeptic base_p4 = base_p2.double_frobenius();
    return base * base_p2 * base_p4;
}

BabyBear BabyBearSeptic::pow_r() const {
    BabyBearSeptic pow_r1 = pow_r_1();
    BabyBearSeptic pow_r = pow_r1 * *this;
    for(uintptr_t i = 1 ; i < 7 ; i++) {
        assert(pow_r.value[i] == BabyBear::zero());
    }
    return pow_r.value[0];
}

BabyBearSeptic BabyBearSeptic::reciprocal() const {
    BabyBearSeptic pow_r_1 = this->pow_r_1();
    BabyBearSeptic pow_r = pow_r_1 * *this;
    return pow_r_1 * pow_r.value[0].reciprocal();
}

BabyBearSeptic BabyBearSeptic::sqrt(BabyBear pow_r) const {
    if (*this == BabyBearSeptic::zero()) {
        return *this;
    }

    BabyBearSeptic n_iter = *this;
    BabyBearSeptic n_power = *this;
    for(uintptr_t i = 1 ; i < 30 ; i++) {
        n_iter *= n_iter;
        if(i >= 26) {
            n_power *= n_iter;
        }
    }

    BabyBearSeptic n_frobenius = n_power.frobenius();
    BabyBearSeptic denominator = n_frobenius;

    n_frobenius = n_frobenius.double_frobenius();
    denominator *= n_frobenius;
    n_frobenius = n_frobenius.double_frobenius();
    denominator *= n_frobenius;
    denominator *= *this;

    BabyBear base = pow_r.reciprocal();
    BabyBear g = BabyBear::from_canonical_u32(31);
    BabyBear a = BabyBear::one();
    BabyBear nonresidue = BabyBear::one() - base;

    while (nonresidue.is_square()) {
        a *= g;
        nonresidue = a.square() - base;
    }

    BabyBearCipolla x = BabyBearCipolla(a, BabyBear::one());
    x = x.pow(1006632961, nonresidue);

    return denominator * x.real;
}

BabyBearSeptic BabyBearSeptic::universal_hash() const {
    return *this * BabyBearSeptic(A_EC_LOGUP) + BabyBearSeptic(B_EC_LOGUP);
}

BabyBearSeptic BabyBearSeptic::curve_formula() const {
    BabyBearSeptic result = (*this * *this + BabyBear::two()) * *this;
    result.value[5] += BabyBear::from_canonical_u32(26);
    return result;
}

bool BabyBearSeptic::is_receive() const {
    uint32_t limb = value[6].as_canonical_u32();
    return 1 <= limb && limb <= (BabyBear::MOD - 1) / 2;
}

bool BabyBearSeptic::is_send() const {
    uint32_t limb = value[6].as_canonical_u32();
    return (BabyBear::MOD + 1) / 2 <= limb && limb <= (BabyBear::MOD - 1);
}

bool BabyBearSeptic::is_exception() const {
    return value[6] == BabyBear::zero();
}

BabyBearCipolla BabyBearCipolla::one() {
    return BabyBearCipolla(BabyBear::one(), BabyBear::zero());
}

BabyBearCipolla BabyBearCipolla::mul_ext(BabyBearCipolla other, BabyBear nonresidue) {
    BabyBear new_real = real * other.real + nonresidue * imag * other.imag;
    BabyBear new_imag = real * other.imag + imag * other.real;
    return BabyBearCipolla(new_real, new_imag);
}

BabyBearCipolla BabyBearCipolla::pow(uint32_t exponent, BabyBear nonresidue) {
    BabyBearCipolla result = BabyBearCipolla::one();
    BabyBearCipolla base = *this;

    while(exponent) {
        if(exponent & 1) {
            result = result.mul_ext(base, nonresidue);
        }
        exponent >>= 1;
        base = base.mul_ext(base, nonresidue);
    }

    return result;
}