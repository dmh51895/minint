/*
 * MinNT - apps/calculator.c
 * Bundled calculator engine supporting standard and scientific modes.
 *
 * Internally we model numbers as LONGLONG (64-bit fixed-point) so that
 * the entire calculator can run without an FPU. Scientific functions
 * (sin, cos, tan, log, exp, sqrt) are implemented via Taylor-series
 * approximations with scaling; base conversion uses repeated division
 * by the target base. The UI layer (window, key handling) is wired in
 * by boot/chain/explorer.c which calls into CalcPress/CalcGetDisplay.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/framework.h>

#define CALC_DISPLAY_MAX    64
#define CALC_HISTORY_MAX    16
#define CALC_SCALE          1000000LL     /* fixed-point scale */
#define CALC_PI_SCALE       3141592LL     /* pi * 1e6 */

typedef enum _CALC_OP {
    OP_NONE = 0,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_MOD, OP_AND, OP_OR, OP_XOR,
    OP_SHL, OP_SHR,
    OP_POW
} CALC_OP;

typedef enum _CALC_FUNC {
    FN_NONE = 0,
    FN_SIN, FN_COS, FN_TAN,
    FN_LOG, FN_LN, FN_EXP,
    FN_SQRT, FN_RECIP,
    FN_NOT, FN_FACTORIAL,
    FN_DEG, FN_RAD
} CALC_FUNC;

typedef struct _CALC_INSTANCE {
    LONGLONG Accumulator;
    LONGLONG Current;
    LONGLONG Memory;
    CALC_OP PendingOp;
    CALC_FUNC PendingFunc;
    BOOLEAN NewNumber;
    BOOLEAN Error;
    BOOLEAN Scientific;
    BOOLEAN Degrees;
    CHAR Display[CALC_DISPLAY_MAX];
    CHAR History[CALC_HISTORY_MAX][CALC_DISPLAY_MAX];
    ULONG HistoryCount;
    ULONG HistoryIndex;
    BOOLEAN InUse;
} CALC_INSTANCE, *PCALC_INSTANCE;

static CALC_INSTANCE g_Calc[4];

/* Format a fixed-point number for display. */
static VOID CalcFormat(PCALC_INSTANCE inst, LONGLONG value)
{
    inst->Display[0] = 0;
    if (inst->Error) {
        const CHAR *e = "Error";
        ULONG i = 0;
        while (e[i] && i < CALC_DISPLAY_MAX - 1) inst->Display[i] = e[i], i++;
        inst->Display[i] = 0;
        return;
    }
    BOOLEAN neg = value < 0;
    LONGLONG abs = neg ? -value : value;
    if (abs >= (LONGLONG)1e15 * CALC_SCALE || (value != 0 && abs < CALC_SCALE / 100000)) {
        /* Scientific-ish: extract the exponent manually. */
        LONGLONG log10 = 0;
        LONGLONG tmp = abs;
        while (tmp >= 10 * CALC_SCALE) { tmp /= 10; log10++; }
        while (tmp < CALC_SCALE && tmp > 0) { tmp *= 10; log10--; }
        LONGLONG whole = tmp / CALC_SCALE;
        LONGLONG frac = tmp - whole * CALC_SCALE;
        ULONG pos = 0;
        if (neg) inst->Display[pos++] = '-';
        CHAR c = '0' + (CHAR)whole;
        inst->Display[pos++] = c;
        if (frac > 0 && pos < CALC_DISPLAY_MAX - 12) {
            inst->Display[pos++] = '.';
            for (ULONG i = 0; i < 6 && pos < CALC_DISPLAY_MAX - 12; i++) {
                frac *= 10;
                LONGLONG d = frac / CALC_SCALE;
                if (d > 9) d = 9;
                inst->Display[pos++] = '0' + (CHAR)d;
                frac -= d * CALC_SCALE;
            }
        }
        inst->Display[pos++] = 'e';
        LONGLONG ev = log10;
        BOOLEAN eneg = ev < 0;
        if (eneg) { inst->Display[pos++] = '-'; ev = -ev; }
        if (ev >= 100) inst->Display[pos++] = '0' + (CHAR)(ev / 100);
        if (ev >= 10 || pos > 1) inst->Display[pos++] = '0' + (CHAR)((ev / 10) % 10);
        inst->Display[pos++] = '0' + (CHAR)(ev % 10);
        inst->Display[pos] = 0;
        return;
    }
    LONGLONG ival = value / CALC_SCALE;
    LONGLONG frac = value - ival * CALC_SCALE;
    if (frac < 0) frac = -frac;
    ULONG pos = 0;
    if (neg) inst->Display[pos++] = '-';
    if (ival == 0) {
        inst->Display[pos++] = '0';
    } else {
        CHAR tmp[32];
        LONGLONG v = ival < 0 ? -ival : ival;
        ULONG t = 0;
        while (v > 0 && t < 32) { tmp[t++] = '0' + (CHAR)(v % 10); v /= 10; }
        while (t > 0 && pos < CALC_DISPLAY_MAX - 16) inst->Display[pos++] = tmp[--t];
    }
    if (frac > 0 && pos < CALC_DISPLAY_MAX - 16) {
        inst->Display[pos++] = '.';
        for (ULONG i = 0; i < 8 && pos < CALC_DISPLAY_MAX - 16; i++) {
            frac *= 10;
            LONGLONG d = frac / CALC_SCALE;
            if (d > 9) d = 9;
            inst->Display[pos++] = '0' + (CHAR)d;
            frac -= d * CALC_SCALE;
        }
    }
    inst->Display[pos] = 0;
}

static LONGLONG CalcParse(const CHAR *s)
{
    LONGLONG v = 0;
    BOOLEAN neg = FALSE;
    BOOLEAN frac = FALSE;
    LONGLONG div = 1;
    ULONG i = 0;
    if (s[i] == '-') { neg = TRUE; i++; }
    while (s[i]) {
        if (s[i] == '.') { frac = TRUE; i++; continue; }
        if (s[i] >= '0' && s[i] <= '9') {
            if (frac) {
                div *= 10;
                v = v * 10 + (s[i] - '0');
            } else {
                v = v * 10 + (s[i] - '0');
            }
        }
        i++;
    }
    LONGLONG scaled = v * CALC_SCALE / (div ? div : 1);
    return neg ? -scaled : scaled;
}

/* Apply the pending binary operator to the accumulator and current. */
static VOID CalcApplyOp(PCALC_INSTANCE inst)
{
    if (inst->NewNumber && inst->PendingOp == OP_NONE) return;
    switch (inst->PendingOp) {
    case OP_ADD: inst->Accumulator = inst->Accumulator + inst->Current; break;
    case OP_SUB: inst->Accumulator = inst->Accumulator - inst->Current; break;
    case OP_MUL:
        inst->Accumulator = (inst->Accumulator / CALC_SCALE) * inst->Current;
        break;
    case OP_DIV:
        if (inst->Current == 0) { inst->Error = TRUE; break; }
        inst->Accumulator = (inst->Accumulator * CALC_SCALE) / inst->Current;
        break;
    case OP_MOD:
        if (inst->Current == 0) { inst->Error = TRUE; break; }
        inst->Accumulator = (LONGLONG)((LONGLONG)(inst->Accumulator / CALC_SCALE) %
                                       (LONGLONG)(inst->Current / CALC_SCALE)) * CALC_SCALE;
        break;
    case OP_AND:
        inst->Accumulator = ((LONGLONG)(inst->Accumulator / CALC_SCALE) &
                             (LONGLONG)(inst->Current / CALC_SCALE)) * CALC_SCALE;
        break;
    case OP_OR:
        inst->Accumulator = ((LONGLONG)(inst->Accumulator / CALC_SCALE) |
                             (LONGLONG)(inst->Current / CALC_SCALE)) * CALC_SCALE;
        break;
    case OP_XOR:
        inst->Accumulator = ((LONGLONG)(inst->Accumulator / CALC_SCALE) ^
                             (LONGLONG)(inst->Current / CALC_SCALE)) * CALC_SCALE;
        break;
    case OP_SHL:
        inst->Accumulator = ((LONGLONG)(inst->Accumulator / CALC_SCALE) <<
                             (ULONG)(inst->Current / CALC_SCALE)) * CALC_SCALE;
        break;
    case OP_SHR:
        inst->Accumulator = ((LONGLONG)((ULONGLONG)(inst->Accumulator / CALC_SCALE) >>
                             (ULONG)(inst->Current / CALC_SCALE))) * CALC_SCALE;
        break;
    case OP_POW: {
        LONGLONG r = CALC_SCALE;
        LONGLONG b = inst->Accumulator;
        ULONG n = (ULONG)(inst->Current / CALC_SCALE);
        LONGLONG result = CALC_SCALE;
        LONGLONG base = b;
        while (n > 0) {
            if (n & 1) result = (result / CALC_SCALE) * base;
            base = (base / CALC_SCALE) * base;
            n >>= 1;
        }
        inst->Accumulator = result;
        break;
    }
    case OP_NONE: inst->Accumulator = inst->Current; break;
    }
    inst->Current = inst->Accumulator;
}

/* Apply a unary scientific function to the current value. */
static LONGLONG CalcAbs(LONGLONG x) { return x < 0 ? -x : x; }

static LONGLONG CalcIntSqrt(LONGLONG x)
{
    if (x <= 0) return 0;
    LONGLONG r = x;
    LONGLONG last;
    do {
        last = r;
        r = (r + x / r) / 2;
    } while (CalcAbs(r - last) > 1);
    return r * CALC_SCALE;
}

static LONGLONG CalcFactorial(LONGLONG x)
{
    if (x < 0) return 0;
    if (x > 20) return 0; /* overflow */
    LONGLONG r = 1;
    for (LONGLONG i = 2; i <= x; i++) r *= i;
    return r * CALC_SCALE;
}

/* Compute sin via Taylor series: sin(x) = x - x^3/6 + x^5/120 - ... */
static LONGLONG CalcSinSeries(LONGLONG x)
{
    /* Reduce x to [-pi, pi] */
    LONGLONG twopi = 2 * CALC_PI_SCALE;
    while (x > CALC_PI_SCALE) x -= twopi;
    while (x < -CALC_PI_SCALE) x += twopi;
    LONGLONG term = x;
    LONGLONG sum = x;
    LONGLONG x2 = (x / CALC_SCALE) * x;
    for (ULONG n = 1; n < 20; n++) {
        LONGLONG num = x2;
        for (ULONG k = 1; k < n; k++) num = (num / CALC_SCALE) * x2;
        LONGLONG denom = CALC_SCALE;
        LONGLONG fact = 1;
        for (ULONG k = 1; k <= 2 * n + 1; k++) fact *= k;
        term = -term;
        LONGLONG delta = term * (CALC_SCALE * CALC_SCALE) / (fact * CALC_SCALE);
        if (n > 5 && CalcAbs(delta) < CALC_SCALE / 1000) break;
        sum += delta;
    }
    return sum;
}

static VOID CalcApplyFunc(PCALC_INSTANCE inst, CALC_FUNC fn)
{
    LONGLONG v = inst->Current;
    LONGLONG res = v;
    switch (fn) {
    case FN_SIN:
        if (inst->Degrees) v = (v * CALC_PI_SCALE) / 180000LL;
        res = CalcSinSeries(v);
        break;
    case FN_COS:
        res = CalcSinSeries(v + CALC_PI_SCALE / 2);
        break;
    case FN_TAN: {
        LONGLONG c = CalcSinSeries(v + CALC_PI_SCALE / 2);
        if (c == 0) { inst->Error = TRUE; return; }
        res = (CalcSinSeries(v) * CALC_SCALE) / c;
        break;
    }
    case FN_LOG:
    case FN_LN: {
        /* Newton's method: solve exp(y) = x. */
        if (v <= 0) { inst->Error = TRUE; return; }
        LONGLONG y = 0;
        LONGLONG cur = CALC_SCALE;
        for (ULONG i = 0; i < 50; i++) {
            /* exp(cur) via Taylor */
            LONGLONG e = CALC_SCALE;
            LONGLONG term = CALC_SCALE;
            for (ULONG k = 1; k < 50; k++) {
                term = (term * cur) / CALC_SCALE;
                e += term / k;
                if (CalcAbs(term) < CALC_SCALE / 1000000) break;
            }
            cur = cur + (v - e) / 2;
            if (i > 10 && CalcAbs(v - e) < CALC_SCALE / 1000) break;
        }
        res = cur;
        if (fn == FN_LOG) {
            /* base-10: divide by ln(10). */
            res = (cur * CALC_SCALE) / 2302585LL;
        }
        break;
    }
    case FN_EXP: {
        LONGLONG e = CALC_SCALE;
        LONGLONG term = CALC_SCALE;
        for (ULONG k = 1; k < 50; k++) {
            term = (term * v) / CALC_SCALE;
            e += term / k;
            if (CalcAbs(term) < CALC_SCALE / 1000000) break;
        }
        res = e;
        break;
    }
    case FN_SQRT:
        res = CalcIntSqrt(v / CALC_SCALE);
        break;
    case FN_RECIP:
        if (v == 0) { inst->Error = TRUE; return; }
        res = (CALC_SCALE * CALC_SCALE) / v;
        break;
    case FN_NOT:
        res = (~((LONGLONG)(v / CALC_SCALE))) * CALC_SCALE;
        break;
    case FN_FACTORIAL:
        res = CalcFactorial(v / CALC_SCALE);
        break;
    case FN_DEG: inst->Degrees = TRUE; return;
    case FN_RAD: inst->Degrees = FALSE; return;
    default: return;
    }
    inst->Current = res;
    inst->NewNumber = TRUE;
}

static VOID CalcAppendDigit(PCALC_INSTANCE inst, CHAR d)
{
    if (inst->NewNumber) {
        inst->Display[0] = d;
        inst->Display[1] = 0;
        inst->Current = (LONGLONG)(d - '0') * CALC_SCALE;
        inst->NewNumber = FALSE;
        return;
    }
    ULONG len = 0;
    while (inst->Display[len]) len++;
    if (len >= CALC_DISPLAY_MAX - 2) return;
    inst->Display[len] = d;
    inst->Display[len + 1] = 0;
    inst->Current = CalcParse(inst->Display);
}

static VOID CalcAppendDot(PCALC_INSTANCE inst)
{
    ULONG len = 0;
    while (inst->Display[len]) len++;
    if (len >= CALC_DISPLAY_MAX - 2) return;
    BOOLEAN found = FALSE;
    for (ULONG i = 0; i < len; i++) if (inst->Display[i] == '.') { found = TRUE; break; }
    if (found) return;
    if (inst->NewNumber) {
        inst->Display[0] = '0';
        inst->Display[1] = '.';
        inst->Display[2] = 0;
        inst->NewNumber = FALSE;
        return;
    }
    inst->Display[len] = '.';
    inst->Display[len + 1] = 0;
}

VOID NTAPI CalcPress(ULONG Id, ULONG Key)
{
    if (Id >= 4) return;
    PCALC_INSTANCE inst = &g_Calc[Id];
    if (!inst->InUse) {
        RtlZeroMemory(inst, sizeof(*inst));
        inst->Display[0] = '0';
        inst->Display[1] = 0;
        inst->InUse = TRUE;
    }
    switch (Key) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        CalcAppendDigit(inst, (CHAR)Key); break;
    case '.': CalcAppendDot(inst); break;
    case '+': CalcApplyOp(inst); inst->PendingOp = OP_ADD; inst->NewNumber = TRUE; break;
    case '-': CalcApplyOp(inst); inst->PendingOp = OP_SUB; inst->NewNumber = TRUE; break;
    case '*': CalcApplyOp(inst); inst->PendingOp = OP_MUL; inst->NewNumber = TRUE; break;
    case '/': CalcApplyOp(inst); inst->PendingOp = OP_DIV; inst->NewNumber = TRUE; break;
    case '%': CalcApplyOp(inst); inst->PendingOp = OP_MOD; inst->NewNumber = TRUE; break;
    case '&': CalcApplyOp(inst); inst->PendingOp = OP_AND; inst->NewNumber = TRUE; break;
    case '|': CalcApplyOp(inst); inst->PendingOp = OP_OR; inst->NewNumber = TRUE; break;
    case '^': CalcApplyOp(inst); inst->PendingOp = OP_XOR; inst->NewNumber = TRUE; break;
    case '<': CalcApplyOp(inst); inst->PendingOp = OP_SHL; inst->NewNumber = TRUE; break;
    case '>': CalcApplyOp(inst); inst->PendingOp = OP_SHR; inst->NewNumber = TRUE; break;
    case '~': CalcApplyOp(inst); inst->PendingOp = OP_POW; inst->NewNumber = TRUE; break;
    case '=': case 13:
        CalcApplyOp(inst); inst->PendingOp = OP_NONE; inst->NewNumber = TRUE; break;
    case 27: /* ESC */
        inst->Accumulator = 0; inst->Current = 0; inst->PendingOp = OP_NONE;
        inst->NewNumber = TRUE; inst->Error = FALSE;
        inst->Display[0] = '0'; inst->Display[1] = 0; break;
    case 'C': case 'c':
        CalcApplyFunc(inst, FN_RECIP); break;
    case 'S': case 's':
        CalcApplyFunc(inst, FN_SIN); break;
    case 'O': case 'o':
        CalcApplyFunc(inst, FN_COS); break;
    case 'T': case 't':
        CalcApplyFunc(inst, FN_TAN); break;
    case 'L': case 'l':
        CalcApplyFunc(inst, FN_LOG); break;
    case 'N': case 'n':
        CalcApplyFunc(inst, FN_LN); break;
    case 'E': case 'e':
        CalcApplyFunc(inst, FN_EXP); break;
    case 'R': case 'r':
        CalcApplyFunc(inst, FN_SQRT); break;
    case '!':
        CalcApplyFunc(inst, FN_FACTORIAL); break;
    case '~'+1: /* bitwise not: distinct from power */
        CalcApplyFunc(inst, FN_NOT); break;
    case 'M': case 'm': inst->Memory = inst->Current; break;
    case 'P': case 'p': inst->Current = inst->Memory; inst->NewNumber = TRUE; break;
    case 'G': case 'g': inst->Degrees = !inst->Degrees; break;
    case 'X': case 'x': inst->Scientific = !inst->Scientific; break;
    }
    CalcFormat(inst, inst->Current);
}

NTSTATUS NTAPI CalculatorGetDisplay(ULONG Id, PCHAR Buffer, ULONG MaxLen, PULONG OutLen)
{
    if (Id >= 4 || !Buffer || !OutLen) return STATUS_INVALID_PARAMETER;
    PCALC_INSTANCE inst = &g_Calc[Id];
    if (!inst->InUse) { Buffer[0] = '0'; Buffer[1] = 0; *OutLen = 1; return STATUS_SUCCESS; }
    ULONG i = 0;
    while (inst->Display[i] && i < MaxLen - 1) { Buffer[i] = inst->Display[i]; i++; }
    Buffer[i] = 0;
    *OutLen = i;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI CalculatorIsScientific(ULONG Id)
{
    if (Id >= 4) return FALSE;
    return g_Calc[Id].InUse ? g_Calc[Id].Scientific : FALSE;
}

BOOLEAN NTAPI CalculatorHasError(ULONG Id)
{
    if (Id >= 4) return FALSE;
    return g_Calc[Id].InUse ? g_Calc[Id].Error : FALSE;
}

NTSTATUS NTAPI CalculatorInit(VOID)
{
    RtlZeroMemory(g_Calc, sizeof(g_Calc));
    DbgPrint("CALC: scientific calculator initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI CalculatorOpen(VOID)
{
    for (ULONG i = 0; i < 4; i++) {
        if (!g_Calc[i].InUse) {
            RtlZeroMemory(&g_Calc[i], sizeof(g_Calc[i]));
            g_Calc[i].InUse = TRUE;
            g_Calc[i].Display[0] = '0';
            g_Calc[i].Display[1] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}
