// ============================================================================
// minicc.c — мини-компилятор: парсер -> байткод -> стековая VM (freestanding)
// ============================================================================
#include "minicc.h"
#include <stdint.h>

#define MINI_MAX_CODE 64
#define MINI_STACK 32
#define MINI_MAX_DEPTH 24
#define MINI_MAX_LEN 192

typedef enum {
    OP_PUSH = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG
} mini_op_t;

typedef struct {
    uint8_t op;
    int32_t arg; // только для OP_PUSH
} mini_insn_t;

typedef struct {
    const char *p;       // курсор парсера
    mini_insn_t *code;   // буфер кода
    int ncode;           // записано инструкций
    int depth;           // глубина рекурсии
    char *err;           // буфер ошибки
    int errn;
    int failed;
} mini_ctx_t;

static void mini_err(mini_ctx_t *c, const char *msg)
{
    if (c->failed)
        return;
    c->failed = 1;
    if (!c->err || c->errn <= 0)
        return;
    int i = 0;
    while (msg[i] && i + 1 < c->errn) {
        c->err[i] = msg[i];
        i++;
    }
    c->err[i] = '\0';
}

static void mini_emit(mini_ctx_t *c, mini_op_t op, int32_t arg)
{
    if (c->failed)
        return;
    if (c->ncode >= MINI_MAX_CODE) {
        mini_err(c, "code buffer full (expr too long)");
        return;
    }
    c->code[c->ncode].op = (uint8_t)op;
    c->code[c->ncode].arg = arg;
    c->ncode++;
}

static void mini_skip(mini_ctx_t *c)
{
    while (*c->p == ' ' || *c->p == '\t')
        c->p++;
}

static int mini_isdigit(char ch) { return ch >= '0' && ch <= '9'; }
static int mini_isxdigit(char ch)
{
    return mini_isdigit(ch) ||
           (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

// Forward decls (взаимная рекурсия expr/term/factor).
static void mini_expr(mini_ctx_t *c);

static void mini_number(mini_ctx_t *c)
{
    mini_skip(c);
    int neg = 0;
    // Унарные знаки обрабатывает factor; здесь — только цифры.
    (void)neg;
    int base = 10;
    int32_t val = 0;
    int nd = 0;
    if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X')) {
        base = 16;
        c->p += 2;
        while (mini_isxdigit(*c->p)) {
            char ch = *c->p++;
            int d = mini_isdigit(ch) ? ch - '0'
                  : (ch >= 'a' ? ch - 'a' + 10 : ch - 'A' + 10);
            val = val * 16 + d;
            nd++;
        }
    } else {
        while (mini_isdigit(*c->p)) {
            val = val * 10 + (*c->p - '0');
            c->p++;
            nd++;
        }
    }
    if (nd == 0) {
        mini_err(c, "number expected");
        return;
    }
    (void)base;
    mini_emit(c, OP_PUSH, val);
}

static void mini_factor(mini_ctx_t *c)
{
    if (c->failed)
        return;
    if (c->depth >= MINI_MAX_DEPTH) {
        mini_err(c, "nesting too deep (max 24)");
        return;
    }
    mini_skip(c);
    if (*c->p == '-') {
        c->p++;
        c->depth++;
        mini_factor(c);
        c->depth--;
        mini_emit(c, OP_NEG, 0);
        return;
    }
    if (*c->p == '+') {
        c->p++;
        c->depth++;
        mini_factor(c);
        c->depth--;
        return;
    }
    if (*c->p == '(') {
        c->p++;
        c->depth++;
        mini_expr(c);
        c->depth--;
        mini_skip(c);
        if (*c->p != ')') {
            mini_err(c, "')' expected");
            return;
        }
        c->p++;
        return;
    }
    mini_number(c);
}

static void mini_term(mini_ctx_t *c)
{
    mini_factor(c);
    while (!c->failed) {
        mini_skip(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%')
            break;
        c->p++;
        mini_factor(c);
        if (c->failed)
            return;
        mini_emit(c, op == '*' ? OP_MUL : op == '/' ? OP_DIV : OP_MOD, 0);
    }
}

static void mini_expr(mini_ctx_t *c)
{
    mini_term(c);
    while (!c->failed) {
        mini_skip(c);
        char op = *c->p;
        if (op != '+' && op != '-')
            break;
        c->p++;
        mini_term(c);
        if (c->failed)
            return;
        mini_emit(c, op == '+' ? OP_ADD : OP_SUB, 0);
    }
}

// Стековая VM: выполняет код, кладёт итог в *out. 0 = ok, -1 = runtime-ошибка.
static int mini_run(const mini_insn_t *code, int n, int *out, char *err,
                    int errn)
{
    int32_t st[MINI_STACK];
    int sp = 0;
    for (int i = 0; i < n; i++) {
        uint8_t op = code[i].op;
        if (op == OP_PUSH) {
            if (sp >= MINI_STACK) {
                if (err && errn > 0) {
                    const char *m = "vm stack overflow";
                    int k = 0;
                    while (m[k] && k + 1 < errn) { err[k] = m[k]; k++; }
                    err[k] = '\0';
                }
                return -1;
            }
            st[sp++] = code[i].arg;
        } else if (op == OP_NEG) {
            if (sp < 1) {
                if (err && errn > 0)
                    err[0] = '\0';
                return -1;
            }
            st[sp - 1] = -st[sp - 1];
        } else {
            if (sp < 2) {
                if (err && errn > 0)
                    err[0] = '\0';
                return -1;
            }
            int32_t b = st[--sp];
            int32_t a = st[--sp];
            int32_t r = 0;
            if (op == OP_ADD) {
                r = a + b;
            } else if (op == OP_SUB) {
                r = a - b;
            } else if (op == OP_MUL) {
                r = a * b;
            } else if (op == OP_DIV || op == OP_MOD) {
                if (b == 0) {
                    if (err && errn > 0) {
                        const char *m = "division by zero";
                        int k = 0;
                        while (m[k] && k + 1 < errn) { err[k] = m[k]; k++; }
                        err[k] = '\0';
                    }
                    return -1;
                }
                // INT_MIN / -1 — UB в C: кап вручную (freestanding без handler).
                if (a == (-2147483647 - 1) && b == -1)
                    r = a; // wrap-договорённость демо-компилятора
                else
                    r = (op == OP_DIV) ? a / b : a % b;
            }
            st[sp++] = r;
        }
    }
    if (sp != 1) {
        if (err && errn > 0)
            err[0] = '\0';
        return -1;
    }
    *out = (int)st[0];
    return 0;
}

static int mini_strlen(const char *s)
{
    int n = 0;
    while (n < MINI_MAX_LEN + 64 && s[n])
        n++;
    return n;
}

int mini_compile_run(const char *src, int *out_result, char *err, int errn)
{
    if (!src || !out_result) {
        if (err && errn > 0)
            err[0] = '\0';
        return -1;
    }
    if (mini_strlen(src) > MINI_MAX_LEN) {
        if (err && errn > 0) {
            const char *m = "input too long (max 192)";
            int k = 0;
            while (m[k] && k + 1 < errn) { err[k] = m[k]; k++; }
            err[k] = '\0';
        }
        return -1;
    }
    mini_insn_t code[MINI_MAX_CODE];
    mini_ctx_t c;
    c.p = src;
    c.code = code;
    c.ncode = 0;
    c.depth = 0;
    c.err = err;
    c.errn = errn;
    c.failed = 0;
    if (err && errn > 0)
        err[0] = '\0';
    mini_expr(&c);
    if (!c.failed) {
        mini_skip(&c);
        if (*c.p != '\0')
            mini_err(&c, "unexpected character");
    }
    if (c.failed)
        return -1;
    if (c.ncode == 0) {
        mini_err(&c, "empty expression");
        return -1;
    }
    return mini_run(code, c.ncode, out_result, err, errn);
}

// --- Verbose-пайплайн для `build`: токены + байткод + запуск -------------------
static void log_put(char *log, int logn, int *pos, const char *s)
{
    while (*s && *pos + 1 < logn) {
        log[*pos] = *s;
        (*pos)++;
        s++;
    }
}

static void log_num(char *log, int logn, int *pos, int32_t v)
{
    char buf[12];
    int neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        char rev[11];
        int n = 0;
        while (v > 0 && n < 10) {
            rev[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (n > 0)
            buf[i++] = rev[--n];
    }
    if (neg && i < 11) {
        for (int k = i; k > 0; k--)
            buf[k] = buf[k - 1];
        buf[0] = '-';
        i++;
    }
    buf[i] = '\0';
    log_put(log, logn, pos, buf);
}

int mini_build_verbose(const char *src, int *out_result, char *log, int logn)
{
    int pos = 0;
    if (log && logn > 0)
        log[0] = '\0';
    if (!src || !out_result)
        return -1;

    // Стадия 1: токены (лёгкий проход без парсера).
    log_put(log, logn, &pos, "tokens: ");
    {
        const char *p = src;
        int n = 0;
        while (*p && n < 24) {
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;
            if (n > 0)
                log_put(log, logn, &pos, " ");
            if ((*p >= '0' && *p <= '9')) {
                int32_t v = 0;
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                    p += 2;
                    while ((*p >= '0' && *p <= '9') ||
                           (*p >= 'a' && *p <= 'f') ||
                           (*p >= 'A' && *p <= 'F')) {
                        int d = (*p >= '0' && *p <= '9') ? *p - '0'
                              : (*p >= 'a' ? *p - 'a' + 10 : *p - 'A' + 10);
                        v = v * 16 + d;
                        p++;
                    }
                } else {
                    while (*p >= '0' && *p <= '9') {
                        v = v * 10 + (*p - '0');
                        p++;
                    }
                }
                log_put(log, logn, &pos, "NUM(");
                log_num(log, logn, &pos, v);
                log_put(log, logn, &pos, ")");
            } else {
                char t[2] = {*p, '\0'};
                log_put(log, logn, &pos, t);
                p++;
            }
            n++;
        }
        log_put(log, logn, &pos, "\n");
    }

    // Стадия 2: компиляция в байткод (тот же парсер, код наружу).
    mini_insn_t code[MINI_MAX_CODE];
    mini_ctx_t c;
    c.p = src;
    c.code = code;
    c.ncode = 0;
    c.depth = 0;
    c.err = 0;
    c.errn = 0;
    c.failed = 0;
    mini_expr(&c);
    if (!c.failed) {
        mini_skip(&c);
        if (*c.p != '\0')
            mini_err(&c, "unexpected character");
    }
    if (c.failed || c.ncode == 0) {
        log_put(log, logn, &pos, "compile: FAILED\n");
        if (log && logn > 0)
            log[pos < logn ? pos : logn - 1] = '\0';
        return -1;
    }
    log_put(log, logn, &pos, "bytecode: ");
    for (int i = 0; i < c.ncode; i++) {
        if (i > 0)
            log_put(log, logn, &pos, " ");
        const char *name = "?";
        switch (code[i].op) {
            case OP_PUSH: name = "PUSH"; break;
            case OP_ADD: name = "ADD"; break;
            case OP_SUB: name = "SUB"; break;
            case OP_MUL: name = "MUL"; break;
            case OP_DIV: name = "DIV"; break;
            case OP_MOD: name = "MOD"; break;
            case OP_NEG: name = "NEG"; break;
            default: break;
        }
        log_put(log, logn, &pos, name);
        if (code[i].op == OP_PUSH) {
            log_put(log, logn, &pos, "(");
            log_num(log, logn, &pos, code[i].arg);
            log_put(log, logn, &pos, ")");
        }
    }
    log_put(log, logn, &pos, "\n");

    // Стадия 3: запуск на VM.
    char rerr[64];
    rerr[0] = '\0';
    int rc = mini_run(code, c.ncode, out_result, rerr, sizeof(rerr));
    if (rc == 0) {
        log_put(log, logn, &pos, "run: OK result=");
        log_num(log, logn, &pos, *out_result);
        log_put(log, logn, &pos, "\n");
    } else {
        log_put(log, logn, &pos, "run: RUNTIME ERROR");
        if (rerr[0]) {
            log_put(log, logn, &pos, " (");
            log_put(log, logn, &pos, rerr);
            log_put(log, logn, &pos, ")");
        }
        log_put(log, logn, &pos, "\n");
    }
    if (log && logn > 0)
        log[pos < logn ? pos : logn - 1] = '\0';
    return rc;
}
