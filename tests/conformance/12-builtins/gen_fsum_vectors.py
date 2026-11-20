#!/usr/bin/env python3
"""Generate the CPython oracle for Float fsum: and its two-sequence forms.

    python3 tests/conformance/12-builtins/gen_fsum_vectors.py

writes _vectors_fsum.st and _vectors_fsum_pow.st next to this script. The
answers are CPython's own math.fsum, run on this interpreter (CPython 3.14 on
glibc Linux when the files were committed):

  fsum:                               math.fsum(xs)
  fsumOfProducts:with:                math.fsum(x * y for x, y in zip(xs, ys))
  fsumOfSquaredDifferences:with:      math.fsum((x - y) * (x - y) for x, y in zip(xs, ys))
  fsumOfPowSquaredDifferences:with:   math.fsum((x - y) ** 2 for x, y in zip(xs, ys))
                                      (_vectors_fsum_pow.st, 0.16.0)

The two-sequence forms are what protoBaseST's vector scores compute
(proto_db vector_backends.py): every product and difference is one IEEE
double operation in CPython, so the oracle is exact.

Every Float travels as the 16 lowercase hex digits of its big-endian IEEE 754
bits, so -0.0, the infinities and NaN payloads are carried exactly, and the
test compares the answer's 8 bytes with the expected ones. An expected value
is such a String, the String 'nan' (below), or one of the two error texts
protoST signals:

  'fsum: intermediate overflow'   (CPython's OverflowError)
  'fsum: -inf + inf'              (CPython's ValueError)

'nan' marks a NaN that no input carried: one made by an invalid operation in
a term (inf * 0, inf - inf). IEEE 754 leaves the sign and payload of such a NaN
to the platform (x86-64 makes 0xfff8000000000000, arm64 0x7ff8000000000000,
and CPython answers whatever its platform makes), so the test accepts any NaN
there. A NaN that comes from an input term keeps its exact bits.

Entry layouts (each an Array):

  VectorsFsum sums         #(name #(x1 x2 ...) expected)
  VectorsFsum products     #(name #(x1 ...) #(y1 ...) expected)
  VectorsFsum sqdiffs      #(name #(x1 ...) #(y1 ...) expected)
  VectorsFsumPow sqpows    #(name #(x1 ...) #(y1 ...) expected libmDependent decisive)

The pow form. CPython's float ** float calls the C library's pow (after its
own sign handling: a negative base is replaced by its absolute value, which
for the exponent 2 changes nothing), so on glibc `d ** 2` is glibc's
pow(d, 2.0). glibc's pow is not correctly rounded: when the exact square of d
lies half-way between two doubles it rounds either way (about half of such
ties differ from d * d, which rounds to even), and it misrounds a few non-tie
squares as well. The pow cases therefore include ties and non-ties where
pow(d, 2.0) and d * d differ, and whose sums differ, so that the vectors fail
an implementation that computes d * d (or lets the compiler fold pow(d, 2.0)
into d * d).

libmDependent is true when at least one term of the case has
(x - y) ** 2 != (x - y) * (x - y) on this interpreter; decisive is true when,
moreover, the fsum of the d * d terms differs from the expected answer (such a
case fails an implementation that squares by multiplying). Its expected value is
glibc's, so the test pins its bytes on Linux only; another C library's pow
may round those squares differently (CPython on that platform would answer
what that pow answers). Every case, on every platform, must also equal
protoST's own `Float fsum: (... collect: [... raisedTo: 2])`, which calls the
same pow. On Windows the pow vectors are not pinned at all: MSVC's pow
rounds some half-way squares that glibc's rounds to even (CI measured 3 of
these cases), so float-fsum-pow-vectors.st is skipped there, and
float-fsum-pow-matches-raisedto.st holds these cases to raisedTo: 2 instead.

One difference from CPython is kept on purpose. When (x - y) ** 2 overflows,
CPython raises OverflowError at the `**`, before math.fsum sees the term;
protoST's `raisedTo: 2` answers infinity there, as the C library's pow does,
and so does the primitive. Such a case expects the sum with an infinite term
(its name says "CPython raises at **").

The random cases use a fixed seed: a rerun leaves `git diff` clean. The file
starts with "EXPECT: generated" like the 15-binary vector modules, and its
name starts with '_', so the conformance runner does not run it on its own.
"""
import math
import platform
import random
import struct
import sys
from fractions import Fraction
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "_vectors_fsum.st"
OUT_POW = HERE / "_vectors_fsum_pow.st"

INF = math.inf
NAN = math.nan
FMAX = sys.float_info.max
TINY = 5e-324                      # the smallest subnormal, 2**-1074
MIN_NORMAL = sys.float_info.min    # 2**-1022

OVERFLOW = "fsum: intermediate overflow"
INF_MINUS_INF = "fsum: -inf + inf"


def bits(x):
    return struct.pack(">d", x).hex()


def from_bits(h):
    return struct.unpack(">d", bytes.fromhex(h))[0]


NEG_NAN = from_bits("fff8000000000000")
PAYLOAD_NAN = from_bits("7ff8000000000123")


def oracle(terms):
    try:
        return bits(math.fsum(terms))
    except OverflowError:
        return OVERFLOW
    except ValueError:
        return INF_MINUS_INF


def ulp_tie(x, y, op):
    """True when the exact x op y lies exactly half-way between two doubles."""
    exact = op(Fraction(x), Fraction(y))
    r = op(x, y)
    if not math.isfinite(r) or r == 0.0:
        return False
    err = abs(exact - Fraction(r))
    return err != 0 and err == Fraction(math.ulp(r)) / 2


# --- fsum: -------------------------------------------------------------------

def sum_cases(rng):
    cases = []

    def add(name, xs):
        cases.append((name, list(xs)))

    # CPython's own test_math.testFsum table.
    add("empty", [])
    add("zero", [0.0])
    add("cpython 1e100 cancel", [1e100, 1.0, -1e100, 1e-100, 1e50, -1.0, -1e50])
    add("cpython 2^53 - 0.5", [2.0**53, -0.5, -2.0**-54])
    add("cpython 2^53 + 1", [2.0**53, 1.0, 2.0**-100])
    add("cpython 2^53 + 10", [2.0**53 + 10.0, 1.0, 2.0**-100])
    add("cpython 2^53 - 4", [2.0**53 - 4.0, 0.5, 2.0**-54])
    add("cpython harmonic 1000", [1. / n for n in range(1, 1001)])
    add("cpython alternating 1000", [(-1.) ** n / n for n in range(1, 1001)])
    add("cpython 1e16 1 1e-16 (half-way correction)", [1e16, 1., 1e-16])
    add("cpython 1e16-2 cancel", [1e16 - 2., 1. - 2.**-53, -(1e16 - 2.), -(1. - 2.**-53)])
    add("cpython partials resize",
        [2.**n - 2.**(n + 50) + 2.**(n + 52) for n in range(-1074, 972, 2)] + [-2.**1022])

    # Near-cancelling sums and the half-way final rounding.
    add("1e16 + 1 - 1e16", [1e16, 1.0, -1e16])
    add("1e-16 1 1e16 (commuted)", [1e-16, 1.0, 1e16])
    add("1 1e16 1e-16", [1.0, 1e16, 1e-16])
    add("1e16 1 (tie, even)", [1e16, 1.0])
    add("1e16 1 -1e-16 (below tie)", [1e16, 1.0, -1e-16])
    add("1e16 3 (tie, odd)", [1e16, 3.0])
    add("1e16 3 -1e-16", [1e16, 3.0, -1e-16])
    add("-1e16 -1 -1e-16", [-1e16, -1.0, -1e-16])
    add("2^53 1 2^-60", [2.0**53, 1.0, 2.0**-60])
    add("2^53 1 -2^-60", [2.0**53, 1.0, -2.0**-60])
    add("2^53 -1 2^-60", [2.0**53, -1.0, 2.0**-60])
    add("0.1 x 10", [0.1] * 10)
    add("0.1 0.2 -0.3", [0.1, 0.2, -0.3])
    add("Shewchuk 1 1e100 1 -1e100", [1.0, 1e100, 1.0, -1e100])
    add("Shewchuk 7 1e100 -7 -1e100 -9e-20 8e-20", [7, 1e100, -7, -1e100, -9e-20, 8e-20])
    add("Shewchuk fifty", [1e308, 1e-308, -1e308, 1e-308] * 5)
    add("1 + many ulps", [1.0] + [2.0**-53] * 5)
    add("1 + 2^-53 (tie)", [1.0, 2.0**-53])
    add("1 + 2^-53 + 2^-106", [1.0, 2.0**-53, 2.0**-106])
    add("1 + 2^-53 - 2^-106", [1.0, 2.0**-53, -2.0**-106])
    add("3 terms, two ties", [1.0, 2.0**-53, 2.0**-53])

    # Zeros and -0.0.
    add("-0.0", [-0.0])
    add("-0.0 -0.0", [-0.0, -0.0])
    add("0.0 -0.0", [0.0, -0.0])
    add("-0.0 0.0", [-0.0, 0.0])
    add("1 -1", [1.0, -1.0])
    add("-1 1", [-1.0, 1.0])
    add("-1 1 -0.0", [-1.0, 1.0, -0.0])
    add("-1.5", [-1.5])

    # Subnormals.
    add("tiny", [TINY])
    add("tiny tiny -tiny", [TINY, TINY, -TINY])
    add("tiny x 7", [TINY] * 7)
    add("min normal - tiny", [MIN_NORMAL, -TINY])
    add("half min normal x 2", [MIN_NORMAL / 2, MIN_NORMAL / 2])
    add("subnormal mix", [3 * TINY, -2.5e-320, 1e-310, -1e-310, 7e-323])
    add("1 + tiny", [1.0, TINY])
    add("1 tiny -1", [1.0, TINY, -1.0])
    add("subnormal cancel to -0", [-TINY, TINY])

    # The overflow paths.
    add("1e308 1e308", [1e308, 1e308])
    add("1e308 1e308 -1e308 (intermediate)", [1e308, 1e308, -1e308])
    add("1e308 -1e308 1e308", [1e308, -1e308, 1e308])
    add("fmax fmax -fmax", [FMAX, FMAX, -FMAX])
    add("fmax -fmax fmax", [FMAX, -FMAX, FMAX])
    add("fmax + half ulp (overflows)", [FMAX, math.ulp(FMAX) / 2])
    add("fmax + below half ulp", [FMAX, math.ulp(FMAX) / 2 - math.ulp(math.ulp(FMAX) / 2)])
    add("fmax + half ulp - tiny", [FMAX, math.ulp(FMAX) / 2, -TINY])
    add("-fmax -fmax", [-FMAX, -FMAX])
    add("fmax 1 -fmax", [FMAX, 1.0, -FMAX])
    add("fmax x 2 after 1", [1.0, FMAX, FMAX])
    add("1e308 x 3 -1e308 x 3", [1e308] * 3 + [-1e308] * 3)

    # Special values (CPython's special_sum and inf_sum).
    add("inf", [INF])
    add("-inf", [-INF])
    add("inf inf", [INF, INF])
    add("inf 1", [INF, 1.0])
    add("1 -inf", [1.0, -INF])
    add("inf -inf", [INF, -INF])
    add("-inf 5 inf", [-INF, 5.0, INF])
    add("nan", [NAN])
    add("nan 1", [NAN, 1.0])
    add("1 nan 2", [1.0, NAN, 2.0])
    add("-nan", [NEG_NAN])
    add("payload nan", [PAYLOAD_NAN])
    add("1 payload nan", [1.0, PAYLOAD_NAN])
    add("inf nan", [INF, NAN])
    add("nan inf", [NAN, INF])
    add("nan -inf", [NAN, -INF])
    add("inf -inf nan", [INF, -INF, NAN])
    add("nan inf -inf", [NAN, INF, -INF])
    add("nan then overflow", [NAN, 1e308, 1e308])
    add("inf then overflow", [INF, 1e308, 1e308])
    add("overflow before inf", [1e308, 1e308, INF])
    add("inf then fmax cancel", [INF, FMAX, -FMAX])
    add("1e308 1e308 nan", [1e308, 1e308, NAN])

    # Long random sequences: spread exponents, mixed signs.
    for k in range(12):
        n = rng.choice([50, 200, 500, 1000])
        xs = [rng.choice([-1, 1]) * rng.random() * 2.0 ** rng.randint(-60, 60) for _ in range(n)]
        add(f"random spread {k} n={n}", xs)
    for k in range(8):
        n = rng.choice([100, 300])
        base = [rng.choice([-1, 1]) * rng.random() * 2.0 ** rng.randint(-1074, 1000) for _ in range(n // 2)]
        xs = base + [-x for x in base[: n // 4]]
        rng.shuffle(xs)
        add(f"random wide exponents {k} n={len(xs)}", xs)
    for k in range(8):
        # Near cancellation: a random sum, its exact negation spread over
        # different terms, plus a few small residues.
        n = 100
        xs = [rng.uniform(-1e6, 1e6) for _ in range(n)]
        s = math.fsum(xs)
        residue = [rng.uniform(-1e-9, 1e-9) for _ in range(3)]
        seq = xs + [-s] + residue
        rng.shuffle(seq)
        add(f"random near-cancel {k}", seq)
    for k in range(6):
        vals = [7, 1e100, -7, -1e100, -9e-20, 8e-20] * 10
        rng.shuffle(vals)
        add(f"Shewchuk shuffled {k}", [float(v) for v in vals])
    for k in range(6):
        n = rng.choice([8, 32, 128])
        add(f"random gauss {k} n={n}", [rng.gauss(0.0, 1.0) for _ in range(n)])
    for k in range(6):
        # Subnormal-heavy random sums.
        n = 60
        add(f"random subnormal {k}", [rng.choice([-1, 1]) * rng.randint(1, 2**52) * TINY for _ in range(n)])
    return cases


# --- two-sequence forms --------------------------------------------------------

def tie_products(rng, count):
    out = []
    while len(out) < count:
        m = rng.getrandbits(52) | 1                # an odd significand
        x = (1.0 + m * 2.0**-52) * 2.0 ** rng.randint(-40, 40)
        y = 1.5 * 2.0 ** rng.randint(-40, 40)
        if rng.random() < 0.5:
            x = -x
        if ulp_tie(x, y, lambda a, b: a * b):
            out.append((x, y))
    return out


def tie_differences(rng, count):
    """Pairs whose difference is exact and whose square is a tie, and pairs
    whose difference itself is a tie."""
    out = []
    while len(out) < count:
        if len(out) % 2 == 0:
            # a - b exact and odd-significand d = x, d * d a tie needs
            # d = odd * 2^k with a 27-bit odd: (2^27 + odd)^2 has 55 bits.
            d = float((1 << 26) | (rng.getrandbits(26) | 1)) * 2.0 ** rng.randint(-30, 30)
            b = rng.uniform(-4.0, 4.0) * 2.0 ** rng.randint(-5, 5)
            a = b + d
            if Fraction(a) - Fraction(b) != Fraction(a - b):
                continue
            if ulp_tie(a - b, a - b, lambda p, q: p * q):
                out.append((a, b))
        else:
            # The difference itself a tie: a = 2^53, b = -1 gives 2^53 + 1.
            e = rng.randint(0, 20)
            a = 2.0 ** (53 + e)
            b = -(2.0 ** e) * rng.choice([1, 3, 5])
            if ulp_tie(a, b, lambda p, q: p - q):
                out.append((a, b))
    return out


def pair_cases(rng, kind):
    cases = []
    term = (lambda x, y: x * y) if kind == "products" else (lambda x, y: (x - y) * (x - y))

    def add(name, xs, ys):
        cases.append((name, list(xs), list(ys)))

    add("empty", [], [])
    add("one", [3.0], [0.5])
    add("-0.0 x 1", [-0.0], [1.0])
    add("-0.0 x -0.0", [-0.0], [-0.0])
    add("-1 x 1 and 1 x 1", [-1.0, 1.0], [1.0, 1.0])
    add("1e16 1 -1e16 x ones", [1e16, 1.0, -1e16], [1.0, 1.0, 1.0])
    add("1e16 1 1e-16 x ones", [1e16, 1.0, 1e-16], [1.0, 1.0, 1.0])
    add("overflow 1e200 x 1e200", [1e200], [1e200])
    add("overflow intermediate", [1e154, 1e154, -1e154], [1e154, 1e154, 1e154])
    add("product overflows to inf", [1e300], [1e300])
    add("inf x 1", [INF], [1.0])
    add("inf x 0 (nan)", [INF], [0.0])
    add("inf and -inf", [INF, -INF], [1.0, 1.0])
    add("inf x -1 and inf", [INF, INF], [-1.0, 1.0])
    add("nan x 2", [NAN], [2.0])
    add("subnormal products", [TINY, 1e-200, -1e-160], [0.5, 1e-120, 1e-160])
    add("product underflows to -0", [-1e-200], [1e-200])
    add("fma-sensitive", [1.0 + 2.0**-30, -1.0], [1.0 + 2.0**-30, 1.0 + 2.0**-29])
    add("fma-sensitive 2", [0.1, -0.1], [0.1, 0.1])
    add("fmax x 1 and fmax x 1", [FMAX, FMAX], [1.0, 1.0])

    if kind == "products":
        ties = tie_products(rng, 24)
        for k, (x, y) in enumerate(ties[:16]):
            add(f"tie product {k}", [x], [y])
        xs = [p[0] for p in ties]
        ys = [p[1] for p in ties]
        add("tie products together", xs, ys)
        add("tie product plus its error", [ties[0][0], 1.0],
            [ties[0][1], float(Fraction(ties[0][0]) * Fraction(ties[0][1]) - Fraction(ties[0][0] * ties[0][1]))])
    else:
        ties = tie_differences(rng, 24)
        for k, (a, b) in enumerate(ties[:16]):
            add(f"tie difference {k}", [a], [b])
        add("tie differences together", [p[0] for p in ties], [p[1] for p in ties])
        add("a == b", [1.5, -2.0, 0.0], [1.5, -2.0, -0.0])
        add("difference overflows", [FMAX], [-FMAX])
        add("inf - inf (nan)", [INF], [INF])

    for k in range(10):
        n = rng.choice([8, 32, 128, 512])
        xs = [rng.gauss(0.0, 1.0) * 2.0 ** rng.randint(-20, 20) for _ in range(n)]
        ys = [rng.gauss(0.0, 1.0) * 2.0 ** rng.randint(-20, 20) for _ in range(n)]
        add(f"random {k} n={n}", xs, ys)
    for k in range(4):
        # Nearly equal vectors: the l2 case near zero distance.
        n = 64
        xs = [rng.uniform(-1.0, 1.0) for _ in range(n)]
        ys = [x + rng.choice([-1, 0, 1]) * math.ulp(x) for x in xs]
        add(f"near-equal {k}", xs, ys)
    out = []
    for (name, xs, ys) in cases:
        expected = oracle([term(x, y) for x, y in zip(xs, ys)])
        if (expected not in (OVERFLOW, INF_MINUS_INF) and math.isnan(from_bits(expected))
                and not any(math.isnan(v) for v in xs + ys)):
            expected = "nan"
        out.append((name, xs, ys, expected))
    return out


# --- the pow form (0.16.0) ------------------------------------------------------

def pow_term(x, y):
    """(x - y) ** 2 as CPython computes it, except that a square that overflows
    is infinity (the C library's pow, and protoST's raisedTo:) instead of
    CPython's OverflowError."""
    d = x - y
    try:
        return d ** 2
    except OverflowError:
        return INF


def pow_differs(d):
    """True when this interpreter's d ** 2 is not the rounded d * d."""
    if not math.isfinite(d):
        return False
    try:
        p = d ** 2
    except OverflowError:
        return False
    return p != d * d


def pow_cases(rng):
    cases = []

    def add(name, xs, ys):
        cases.append((name, list(xs), list(ys)))

    # The edge cases of the other two-sequence forms.
    add("empty", [], [])
    add("one", [3.0], [0.5])
    add("-0.0 - 1", [-0.0], [1.0])
    add("-0.0 - -0.0", [-0.0], [-0.0])
    add("0.0 - -0.0 and -0.0 - 0.0", [0.0, -0.0], [-0.0, 0.0])
    add("negative differences", [-1.0, 1.0, -3.5], [1.0, 3.0, 0.25])
    add("1e8 1 -1e8 against zeros", [1e8, 1.0, -1e8], [0.0, 0.0, 0.0])
    add("a == b", [1.5, -2.0, 0.0], [1.5, -2.0, -0.0])
    add("sum overflows (intermediate)", [1e154, 1e154, 1.0], [0.0, -0.0, 1.0])
    add("sum overflows then cancels", [1e154, 1e154, 0.0], [0.0, 0.0, 0.0])
    add("square overflows, CPython raises at **", [1e200], [0.0])
    add("square overflows twice, CPython raises at **", [1e200, -1e300], [-1e200, 1e300])
    add("difference overflows", [FMAX], [-FMAX])
    add("inf - 1", [INF], [1.0])
    add("1 - inf and inf - 1", [1.0, INF], [INF, 1.0])
    add("inf - inf (nan)", [INF], [INF])
    add("nan - 2", [NAN], [2.0])
    add("-nan - 2", [NEG_NAN], [2.0])
    add("payload nan - 2", [PAYLOAD_NAN], [2.0])
    add("nan and inf", [NAN, INF], [0.0, 0.0])
    add("square subnormal", [1e-160, -1e-160], [0.0, 0.0])
    add("square underflows to 0", [TINY, -TINY], [0.0, 0.0])
    add("tiny differences", [MIN_NORMAL, 3 * TINY], [MIN_NORMAL - TINY, 0.0])
    add("fma-sensitive", [1.0 + 2.0**-30, -1.0], [0.0, 2.0**-29])

    # Squares that are exact ties: d with 27 significant bits, so d * d has 54.
    ties_differ, ties_same = [], []
    while len(ties_differ) < 16 or len(ties_same) < 8:
        d = float((1 << 26) | (rng.getrandbits(26) | 1)) * 2.0 ** rng.randint(-30, 30)
        b = rng.uniform(-4.0, 4.0) * 2.0 ** rng.randint(-5, 5)
        a = b + d
        if Fraction(a) - Fraction(b) != Fraction(a - b):
            continue
        if not ulp_tie(a - b, a - b, lambda p, q: p * q):
            continue
        if rng.random() < 0.5:
            a, b = b, a                              # a negative difference
        (ties_differ if pow_differs(a - b) else ties_same).append((a, b))
    ties_differ, ties_same = ties_differ[:16], ties_same[:8]
    for k, (a, b) in enumerate(ties_differ[:12]):
        add(f"tie square, pow differs from d*d {k}", [a], [b])
    for k, (a, b) in enumerate(ties_same[:4]):
        add(f"tie square, pow equals d*d {k}", [a], [b])
    mixed = ties_differ + ties_same
    rng.shuffle(mixed)
    add("tie squares together", [p[0] for p in mixed], [p[1] for p in mixed])

    # The difference itself a tie (2^53 + 1 rounds to even), then squared.
    for k in range(4):
        e = rng.randint(0, 20)
        a = 2.0 ** (53 + e)
        b = -(2.0 ** e) * rng.choice([1, 3, 5])
        add(f"tie difference squared {k}", [a], [b])

    # Non-tie squares that glibc's pow misrounds.
    misrounded = []
    while len(misrounded) < 24:
        d = rng.uniform(1.0, 2.0) * 2.0 ** rng.randint(-300, 300)
        if rng.random() < 0.5:
            d = -d
        if pow_differs(d) and not ulp_tie(d, d, lambda p, q: p * q):
            b = 0.0 if rng.random() < 0.5 else -0.0
            misrounded.append((d + b, b))
    for k, (a, b) in enumerate(misrounded[:8]):
        add(f"misrounded square {k}", [a], [b])
    add("misrounded squares together", [p[0] for p in misrounded], [p[1] for p in misrounded])

    # Random vectors, the k-means shapes.
    for k in range(8):
        n = rng.choice([8, 32, 128, 512])
        xs = [rng.gauss(0.0, 1.0) * 2.0 ** rng.randint(-20, 20) for _ in range(n)]
        ys = [rng.gauss(0.0, 1.0) * 2.0 ** rng.randint(-20, 20) for _ in range(n)]
        add(f"random {k} n={n}", xs, ys)
    for k in range(6):
        # Embedding-like: unit-range point against a centroid (a mean of points).
        n = rng.choice([16, 64, 256])
        pts = [[rng.uniform(-1.0, 1.0) for _ in range(n)] for _ in range(5)]
        centroid = [math.fsum(col) / 5 for col in zip(*pts)]
        add(f"point to centroid {k} n={n}", pts[0], centroid)
    for k in range(4):
        # Nearly equal vectors: the distance near zero.
        n = 64
        xs = [rng.uniform(-1.0, 1.0) for _ in range(n)]
        ys = [x + rng.choice([-1, 0, 1]) * math.ulp(x) for x in xs]
        add(f"near-equal {k}", xs, ys)

    out = []
    for (name, xs, ys) in cases:
        expected = oracle([pow_term(x, y) for x, y in zip(xs, ys)])
        if (expected not in (OVERFLOW, INF_MINUS_INF) and math.isnan(from_bits(expected))
                and not any(math.isnan(v) for v in xs + ys)):
            expected = "nan"
        libm = any(pow_differs(x - y) for x, y in zip(xs, ys))
        decisive = libm and expected != oracle([(x - y) * (x - y) for x, y in zip(xs, ys)])
        out.append((name, xs, ys, expected, libm, decisive))
    return out


# --- output --------------------------------------------------------------------

def st_string(s):
    return "'" + s.replace("'", "''") + "'"


def st_floats(xs):
    return "#(" + " ".join(st_string(bits(x)) for x in xs) + ")"


def main():
    rng = random.Random(20261120)
    sums = [(name, xs, oracle(xs)) for (name, xs) in sum_cases(rng)]
    products = pair_cases(rng, "products")
    sqdiffs = pair_cases(rng, "sqdiffs")

    lines = [
        '"EXPECT: generated"',
        '"Generated by gen_fsum_vectors.py from CPython ' + sys.version.split()[0]
        + "'s math.fsum. Do not edit.\"",
        '"Floats are 16 hex digits of their big-endian IEEE 754 bits. An expected'
        ' value is such a String, the error text, or nan for a NaN made by an invalid'
        ' operation (any NaN matches)."',
        "Object subclass: #VectorsFsum instanceVariableNames: ''.",
        "",
    ]
    # One method per entry keeps each compiled method small.
    def emit(group, entries, render):
        sels = []
        for i, e in enumerate(entries):
            sel = f"{group}{i + 1}"
            sels.append(sel)
            lines.append(f"VectorsFsum class >> {sel}")
            lines.append("  ^ " + render(e) + ".")
            lines.append("")
        lines.append(f"VectorsFsum class >> {group}")
        lines.append(f'  "Every {group} entry."')
        lines.append("  ^ {")
        for sel in sels:
            lines.append(f"    self {sel}.")
        lines.append("  }.")
        lines.append("")

    emit("sums", sums,
         lambda e: "{ " + st_string(e[0]) + ". " + st_floats(e[1]) + ". " + st_string(e[2]) + " }")
    emit("products", products,
         lambda e: "{ " + st_string(e[0]) + ". " + st_floats(e[1]) + ". " + st_floats(e[2])
         + ". " + st_string(e[3]) + " }")
    emit("sqdiffs", sqdiffs,
         lambda e: "{ " + st_string(e[0]) + ". " + st_floats(e[1]) + ". " + st_floats(e[2])
         + ". " + st_string(e[3]) + " }")

    OUT.write_text("\n".join(lines))
    errors = sum(1 for e in sums if e[2] in (OVERFLOW, INF_MINUS_INF)) + sum(
        1 for e in products + sqdiffs if e[3] in (OVERFLOW, INF_MINUS_INF))
    made_nans = sum(1 for e in products + sqdiffs if e[3] == "nan")
    print(f"{OUT.name}: {len(sums)} sums, {len(products)} products, {len(sqdiffs)} squared "
          f"differences ({len(sums) + len(products) + len(sqdiffs)} cases, {errors} of them errors, "
          f"{made_nans} NaNs made by an invalid operation)")

    # The pow form, in its own file and from its own seed, so that the file
    # above stays as 0.15.0 generated it.
    libc = platform.libc_ver()
    if sys.platform != "linux" or libc[0] != "glibc":
        sys.exit("the pow vectors pin glibc's pow: run this generator on glibc Linux")
    sqpows = pow_cases(random.Random(20261121))
    lines = [
        '"EXPECT: generated"',
        '"Generated by gen_fsum_vectors.py from CPython ' + sys.version.split()[0]
        + "'s math.fsum and float ** on glibc " + libc[1] + '. Do not edit."',
        '"Floats are 16 hex digits of their big-endian IEEE 754 bits. An expected'
        ' value is such a String, the error text, or nan for a NaN made by an invalid'
        ' operation (any NaN matches). libmDependent: some term has (x - y) ** 2 ~='
        ' (x - y) * (x - y) on glibc, so the expected bytes are glibc pow\'s.'
        ' decisive: the d * d terms would sum to another answer."',
        "Object subclass: #VectorsFsumPow instanceVariableNames: ''.",
        "",
    ]
    sels = []
    for i, e in enumerate(sqpows):
        sel = f"sqpows{i + 1}"
        sels.append(sel)
        lines.append(f"VectorsFsumPow class >> {sel}")
        lines.append("  ^ { " + st_string(e[0]) + ". " + st_floats(e[1]) + ". " + st_floats(e[2])
                     + ". " + st_string(e[3]) + ". " + ("true" if e[4] else "false")
                     + ". " + ("true" if e[5] else "false") + " }.")
        lines.append("")
    lines.append("VectorsFsumPow class >> sqpows")
    lines.append('  "Every sqpows entry."')
    lines.append("  ^ {")
    for sel in sels:
        lines.append(f"    self {sel}.")
    lines.append("  }.")
    lines.append("")
    OUT_POW.write_text("\n".join(lines))
    errors = sum(1 for e in sqpows if e[3] in (OVERFLOW, INF_MINUS_INF))
    made_nans = sum(1 for e in sqpows if e[3] == "nan")
    libm = sum(1 for e in sqpows if e[4])
    decisive = sum(1 for e in sqpows if e[5])
    raises = sum(1 for e in sqpows if "CPython raises at **" in e[0])
    print(f"{OUT_POW.name}: {len(sqpows)} pow squared differences ({errors} of them errors, "
          f"{made_nans} NaNs made by an invalid operation, {libm} libm-dependent, "
          f"{decisive} decisive against d * d, {raises} where CPython raises at **)")


if __name__ == "__main__":
    main()
