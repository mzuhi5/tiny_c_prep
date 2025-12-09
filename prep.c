/*
 * Tiny C Preprocessor
 * Copyright (c) 2025 mzuhi5
 */

#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    TK_SPACES,
    TK_NEWLINE,
    TK_DIRECTIVE,
    TK_IDENT,
    TK_NUM,
    TK_RESERVED,
    TK_CH,
    TK_LITERAL,
    TK_USR_SRC,
    TK_SYSTEM_SRC,
    TK_EOF,
} Kind;

typedef struct _Keyword Keyword;
typedef struct _Token Token;
typedef struct _Macro Macro;
typedef struct _UsedMacro UsedMacro;
typedef struct _Env Env;
typedef struct _Predefined Predefined;

struct _Keyword {
    char *word;
    Keyword *next;
};

struct _Token {
    Kind id;
    char *pos;
    int len;
    Token *leadings;
    Env *env;
    Token *macro_org;
    UsedMacro *used;
    Token *next;
};

struct _Macro {
    char *key;
    Token *params;
    Token *to;
    Macro *next;
};

struct _UsedMacro {
    Macro *macro;
    UsedMacro *next;
};

struct _Env {
    char *path;
    int skips;
    char *input;
    char *pos;
    Token *cur;
    Env *next;
};

static void env_push();
static void env_pop();
static Token *expand_macro(Token **saddr);
static Token *expand_recursive(Token **saddr);
static Token *expand_obj(Token **saddr, Macro *macro);
static Token *expand_func(Token **saddr, Macro *macro);
static int expr();
static void stmt(int is_top);
static void stmt_off();

struct IncDir {
    char *dir[100];
    int len;
} incdir = {{"/usr/include/", "/usr/include/x86_64-linux-gnu/",
             "/usr/local/include/",
             "/usr/lib/gcc/x86_64-linux-gnu/13/include/"},
            4};

struct _Predefined {
    Kind id;
    char *name;
    char *value;
} predefined[] = {
    // added GNUC and GNUC_MINOR to pass gcc compiling process later
    {TK_NUM, "__FILE__", ""},       {TK_NUM, "__LINE__", ""},
    {TK_NUM, "__x86_64", "1"},      {TK_NUM, "__x86_64__", "1"},
    {TK_NUM, "__VERSION__", "0.1"}, {TK_NUM, "__STDC_VERSION__", "201112L"},
    {TK_NUM, "__STDC__", "1"},      {TK_NUM, "__STDC_HOSTED__", "1"},
    {TK_NUM, "__GNUC__", "13"},     {TK_NUM, "__GNUC_MINOR__", "3"},
    {TK_EOF, NULL, NULL},
};

char *pos = NULL;        // position in input strings
Token *cur = NULL;       // current input token
Token *ocur = NULL;      // output token list
Token *macro_org = NULL; // keep original macro for expansion
Env *env = NULL;     // environment having file, input string, pos, cur, etc.
Macro *macro = NULL; // keep defined macro
Keyword *keyword = NULL; // keyword strings used for parsing input strings

static int scmp(char *p, int len, char *s) {
    return len == strlen(s) && strncmp(p, s, len) == 0;
}

static char *mk_path(char *buf, char *dir, char *fname) {
    char *path = realloc(buf, sizeof(char) * (strlen(dir) + strlen(fname)) + 2);
    sprintf(path, "%s/%s", dir, fname);
    return path;
}

static int linenum(Token *at) {
    int lnnum = 1;
    for (char *p = at->env->input; p < at->pos; p++) {
        lnnum = *p == '\n' ? lnnum + 1 : lnnum;
    }
    return lnnum;
}

static void exit_if(int c, Token *t, char *msg, ...) {
    if (!c) {
        return;
    }
    va_list ap;
    va_start(ap, msg);
    if (t) {
        int lnnum = linenum(t);
        char *lns = t->pos;
        while (lns > env->input && lns[-1] != '\n') {
            lns--;
        }
        char *lne = strchr(t->pos, '\n');
        lne = lne ? lne : t->pos + strlen(t->pos);

        dprintf(2, "%s %d:%d ", env->path, lnnum, (int)(t->pos - lns));
        vdprintf(2, msg, ap);
        dprintf(2, "\n%.*s\n", (int)(lne - lns), lns);
        dprintf(2, "%*s^", (int)(t->pos - lns - 1), " ");
    } else {
        vdprintf(2, msg, ap);
    }
    dprintf(2, "\n");
    if (c != 2) {
        exit(EXIT_FAILURE);
    }
}

static char *read_file(char *path) {
    int fd = open(path, 0);
    exit_if(fd < 0, cur, "Can not open file: %s", path);
    int n, len = 0;
    char *buf = calloc(sizeof(char), len + 2);
    while ((n = read(fd, buf + len, len + 1))) {
        exit_if(n < 1, cur, "Can not read file: %s", path);
        len += n;
        buf[len] = 0;
        buf = realloc(buf, (len + 1) * 2);
    }
    close(fd);
    return buf;
};

static void keywords_init() {
    char *p = "include_next include define undef defined "
              "warinig error ifdef ifndef if else elif endif "
              ">> << == != <= >= -- ++ && || += -= %= /= *= ## ...";
    for (; *p; p++) {
        char *ps = p;
        while (*p && *p != ' ') {
            p++;
        }
        Keyword *kw = calloc(sizeof(Keyword), 1);
        kw->word = strndup(ps, p - ps);
        kw->next = keyword;
        keyword = kw;
    }
}

static int is_keyword(char *p, int len) {
    Keyword *kw = keyword;
    while (kw && !scmp(p, len, kw->word)) {
        kw = kw->next;
    }
    return kw != NULL;
}

static Token *token_new(Kind id, char *ps, char *p) {
    Token *t = calloc(sizeof(Token), 1);
    t->id = id;
    t->pos = ps;
    t->len = p - ps;
    t->env = env;
    return t;
}

static Token *token_stitch(Token *t, Token *prev) {
    return prev ? prev->next = t : t;
}

static Token *token_dup(Token *src) {
    Token *dest = malloc(sizeof(Token));
    return memcpy(dest, src, sizeof(Token));
}

static Token *token_instant(Kind id, char *s) {
    return token_new(id, s, s + strlen(s));
}

static void token_concat(Token *dest, Token *delim) {
    dest->pos = strndup(dest->pos, dest->len);
    for (Token *t = dest->next; t != delim; t = t->next) {
        dest->pos = realloc(dest->pos, dest->len += t->len);
        strncat(dest->pos, t->pos, t->len);
    }
    dest->next = delim;
}

static Token *token_quoted(char *ps, char delim) {
    for (int flg = 0; *pos && (*pos != delim || flg); pos++) {
        flg = *pos == '\\' ? 1 : 0;
    }
    Token *t = token_new(delim == '\'' ? TK_CH : TK_LITERAL, ps, pos++);
    exit_if(!*(pos - 1), t, "No closing quote");
    return t;
}

static int comments() {
    if (strncmp(pos, "//", 2) == 0) {
        while (*pos && *pos != '\n') {
            pos++;
        }
        return 1;
    } else if (strncmp(pos, "/*", 2) == 0) {
        while (strncmp(pos, "*/", 2) != 0) {
            pos++;
        }
        pos += 2;
        return 1;
    }
    return 0;
}

static Token *token_spaces() {
    char *ps = pos;
    while (*pos == ' ' || *pos == '\t' || scmp(pos, 2, "\\\n")) {
        pos = scmp(pos, 2, "\\\n") ? pos + 2 : pos + 1;
    }
    return ps == pos ? NULL : token_new(TK_SPACES, ps, pos);
}

static Token *token_next() {
    Token *t = NULL;
    Token *leadings = NULL;
    static Kind preid = TK_NEWLINE;

    while (*pos) {
        char *ps = pos;
        if (comments()) {
            continue;
        } else if ((t = token_spaces())) {
            leadings = t;
            continue;
        }

        if (*pos == '#' && (preid == TK_NEWLINE || preid == TK_EOF)) {
            t = token_new(TK_DIRECTIVE, ps, ++pos);
        } else if (*pos == '\n') {
            t = token_new(TK_NEWLINE, ps, ++pos);
        } else if (*pos == '"') {
            t = token_quoted(++pos, '"');
        } else if (*pos == '\'') {
            t = token_quoted(++pos, '\'');
        } else if (isdigit(*pos)) {
            while (isdigit(*pos)) {
                pos++;
            }
            pos = (*pos == 'L' || *pos == 'F') ? pos + 1 : pos;
            t = token_new(TK_NUM, ps, pos);
        } else if (isalpha(*pos) || *pos == '_') {
            while (isalnum(*pos) || *pos == '_') {
                pos++;
            }
            if (is_keyword(ps, pos - ps)) {
                t = token_new(TK_RESERVED, ps, pos);
            } else {
                t = token_new(TK_IDENT, ps, pos);
            }
        } else if (is_keyword(ps, 3)) {
            t = token_new(TK_RESERVED, ps, pos += 3);
        } else if (is_keyword(ps, 2)) {
            t = token_new(TK_RESERVED, ps, pos += 2);
        } else if (*ps) {
            t = token_new(TK_RESERVED, ps, ++pos);
        } else {
            exit_if(1, cur, "Not valid character"); // never
        }
        t->leadings = leadings;
        preid = t->id;
        return t;
    }
    t = token_new(TK_EOF, pos, pos);
    t->leadings = leadings;
    preid = t->id;
    return t;
}

static int token_cmp(Token *t, char *s) {
    return t && (t->id == TK_IDENT || t->id == TK_RESERVED) &&
           scmp(t->pos, t->len, s);
}

static Token *consume_any() {
    Token *t = cur;
    cur = cur->next ? cur->next : token_next();
    return t;
}

static Token *consume(char *s) {
    return token_cmp(cur, s) ? consume_any() : NULL;
}

static Token *consume_id(Kind id) {
    return cur->id == id ? consume_any() : NULL;
}

static Token *expect(char *s) {
    Token *t = consume(s);
    exit_if(t == NULL, cur, "Expected token: %s", s);
    return t;
}

static Token *expect_id(Kind id) {
    Token *t = consume_id(id);
    exit_if(t == NULL, cur, "Expected token id: %d", id);
    return t;
}

static Token *token_norm_args(Token *ts) {
    // take care pattern of fn(1, , 3)
    int depth = 0;
    for (Token *t = ts; t; t = t->next) {
        depth += token_cmp(t, "(") ? 1 : 0;
        depth -= token_cmp(t, ")") ? 1 : 0;
        if (depth == 1 && ((token_cmp(t, "(") && token_cmp(t->next, ",")) ||
                           (token_cmp(t, ",") && token_cmp(t->next, ")")) ||
                           (token_cmp(t, ",") && token_cmp(t->next, ",")))) {
            Token *tt = token_new(TK_IDENT, t->next->pos, t->next->pos);
            tt->next = t->next;
            t->next = tt;
        }
    }
    return ts;
}

static Token *consume_to_lnend() {
    Token head = {0};
    for (Token *t = &head; !consume_id(TK_NEWLINE);) {
        t = token_stitch(consume_any(), t);
    }
    return head.next;
}

static Token *consume_func_args() {
    Token *head = cur;
    int depth = 0;
    for (Token *t = head; cur->id != TK_EOF;) {
        t = token_stitch(consume_any(), t);
        depth += token_cmp(t, "(") ? 1 : 0;
        depth -= token_cmp(t, ")") ? 1 : 0;
        if (!depth && token_cmp(t, ")")) {
            break;
        }
    }
    return token_norm_args(head);
}

static Token *token_next_arg_delim(Token *t) {
    for (int depth = 0; t; t = t->next) {
        depth += token_cmp(t, "(") ? 1 : 0;
        depth -= token_cmp(t, ")") ? 1 : 0;
        if ((depth == 0 && token_cmp(t, ",")) ||
            (depth < 0 && token_cmp(t, ")"))) {
            return t;
        }
    }
    return t;
}

static void macro_add(char *key, Token *params, Token *to) {
    Macro *m = calloc(sizeof(Macro), 1);
    m->key = key;
    m->to = to ? token_norm_args(to) : token_instant(TK_SPACES, "");
    m->next = macro;
    macro = m;

    if (params) {
        Token **t = &params->next;
        // rm ','/')' from token list. note each param has only one token.
        for (; !token_cmp(*t, ")"); t = &(*t)->next) {
            *t = token_cmp(*t, ",") ? (*t)->next : *t;
            exit_if(!(*t)->len, (*t), "Expected param name");
        }
        *t = NULL; // use last ')' token as null termination
        m->params = params->next;
    }
}

static Macro *macro_get(Token *t0, Token *t1) {
    for (Macro *m = macro; m; m = m->next) {
        if (scmp(t0->pos, t0->len, m->key) &&
            ((m->params && token_cmp(t1, "(")) ||
             (!m->params && !token_cmp(t1, "(")))) {
            return m;
        }
    }
    return NULL;
}

static void macro_rm(Token *t) {
    Macro **m = &macro;
    while (*m && !scmp(t->pos, t->len, (*m)->key)) {
        m = &(*m)->next;
    }
    *m = *m ? (*m)->next : *m;
}

static void usedmacro_merge(UsedMacro **dest, UsedMacro *add) {
    for (UsedMacro *ua = add; ua;) {
        UsedMacro **ud = dest;
        while (*ud && (*ud)->macro != ua->macro) {
            ud = &(*ud)->next;
        }
        if (*ud) {
            ua = ua->next;
            continue;
        }
        *ud = ua;
        ua = ua->next;
        (*ud)->next = NULL;
    }
}

static Token *expand_recursive_list(Token **taddr) {
    Token head = {0};
    for (Token *prev = &head; *taddr; *taddr = (*taddr)->next) {
        token_stitch(expand_recursive(taddr), prev);
        prev = *taddr;
        if (!(*taddr)->next) {
            break;
        }
    }
    return head.next;
}

static Token *token_stringify(Token *dest, Token *ts, Token *delim) {
    token_concat(ts, delim);
    dest->pos = ts->pos;
    dest->len = ts->len;
    dest->leadings = token_instant(TK_SPACES, " ");
    dest->id = TK_LITERAL;
    return dest;
}

static Token **token_replace_arg(Token **taddr, Token *start, Token *delim) {

    Token *next = (*taddr)->next;
    UsedMacro *used = (*taddr)->used;
    start->leadings = (*taddr)->leadings;

    for (Token *t = start; t != delim; t = t->next, taddr = &(*taddr)->next) {
        *taddr = token_dup(t); // dup for case of f(x) => x x
        usedmacro_merge(&(*taddr)->used, used);
        if ((*taddr)->next == delim) {
            break;
        }
    }
    (*taddr)->next = next;
    return taddr;
}

static Token *token_skip_after_func(Token *t) {
    for (t = t->next; !token_cmp(t, ")");) {
        t = token_next_arg_delim(t->next);
    }
    return t->next;
}

static Token *token_matched_arg(Token *ag, Macro *m, Token **saddr) {

    Token *ts = (*saddr)->next->next; // skip function name and '('
    Token *pm = m->params;
    for (int i = 0; ag && pm; i++, pm = pm->next) {
        if (ag->len == pm->len && strncmp(ag->pos, pm->pos, pm->len) == 0) {
            for (Token *prev = ts; i-- > -1; prev = (*saddr)->next) {
                *saddr = token_next_arg_delim(ts = prev);
            }
            return ts;
        }
    }
    return NULL;
}

static Token *expand_def(Macro *m, UsedMacro *used) {
    Token head = {0};
    Token *t = m->to;
    for (Token *prev = &head; t; prev = t, t = t->next) {
        t = token_dup(t);
        t->used = calloc(sizeof(UsedMacro), 1);
        t->used->macro = m;
        t->used->next = used;
        t = token_stitch(t, prev);
    }
    return head.next;
}

static Token *expand_func(Token **saddr, Macro *m) {
    // expand arguments before other macro expansion
    Token *t = (*saddr)->next->next;
    Token *te = token_skip_after_func(*saddr);
    for (Token *prev = (*saddr)->next; t != te; t = t->next) {
        token_stitch(expand_recursive(&t), prev);
        prev = t;
    }

    // expand macro and matched params with actual args
    Token *head = expand_def(m, (*saddr)->used);
    head->leadings = (*saddr)->leadings;
    Token **taddr = &head;

    for (Token *prev = NULL; *taddr; prev = *taddr, taddr = &(*taddr)->next) {
        Token *tdelim = *saddr;
        Token *ts = NULL;
        if (token_cmp(*taddr, "#")) {
            exit_if(!(*taddr)->next, *taddr, "Bad use of '#'");

            *taddr = (*taddr)->next; // remove '#'
            if ((ts = token_matched_arg(*taddr, m, &tdelim))) {
                *taddr = token_stringify(*taddr, ts, tdelim);
            }

            exit_if(!ts, *taddr, "No following parameter to '#'");
        } else if (token_cmp(*taddr, "##")) {
            exit_if(!prev || !(*taddr)->next, *taddr, "Bad use of '##'");

            prev->next = *taddr = (*taddr)->next; // remove '##'
            (*taddr)->leadings = NULL;
            if ((ts = token_matched_arg(*taddr, m, &tdelim))) {
                taddr = token_replace_arg(taddr, ts, tdelim);
            }
            token_concat(prev, (*taddr)->next);
            taddr = &prev;

        } else if (token_cmp(*taddr, "__VA_ARGS__")) {

            Token *tp = token_instant(TK_RESERVED, "...");
            ts = token_matched_arg(tp, m, &tdelim);
            exit_if(!ts, *taddr, "No matched func param(...) for __VA_ARGS__");

            while (!token_cmp(tdelim, ")")) {
                tdelim = tdelim->next;
            }
            taddr = token_replace_arg(taddr, ts, tdelim);

        } else if ((ts = token_matched_arg(*taddr, m, &tdelim))) {
            taddr = token_replace_arg(taddr, ts, tdelim);
        }
    }

    // stitch tokens and recur macro expansion
    Token *tt = head;
    head = expand_recursive_list(&tt);
    tt->next = token_skip_after_func(*saddr);
    *saddr = tt;
    return head;
}

static Token *expand_obj(Token **saddr, Macro *m) {
    Token *t = expand_def(m, (*saddr)->used);
    Token *head = expand_recursive_list(&t);
    t->next = (*saddr)->next;
    *saddr = t;
    return head;
}

static Token *expand_recursive(Token **saddr) {
    if (token_cmp(*saddr, "__LINE__") || token_cmp(*saddr, "__FILE__")) {
        (*saddr)->macro_org = macro_org;
        return *saddr;
    }

    Macro *m = NULL;
    if (!(m = macro_get(*saddr, (*saddr)->next))) {
        return *saddr;
    }

    for (UsedMacro *used = (*saddr)->used; used; used = used->next) {
        if (token_cmp(*saddr, used->macro->key)) {
            return *saddr;
        }
    }

    if (m->params && ((*saddr)->next && token_cmp((*saddr)->next, "("))) {
        return expand_func(saddr, m);
    }
    return expand_obj(saddr, m);
}

static Token *expand_macro(Token **saddr) {
    Macro *m = NULL;
    if (!(m = macro_get(*saddr, cur))) {
        return *saddr;
    }

    if (m->params && (cur && token_cmp(cur, "("))) {
        token_stitch(consume_func_args(), *saddr);
    }

    Token *leadings = (*saddr)->leadings;
    Token *t = expand_recursive(saddr);
    t->leadings = leadings;
    return t;
}

static char *inc_path_find(char *fname, int *skips, int is_local) {
    char *path = NULL;
    if (is_local && *skips == 0) { // check current dir first
        path = mk_path(path, dirname(strdup(env->path)), fname);
        if (access(path, R_OK) == 0) {
            return path;
        }
    }
    for (int i = 0; i < incdir.len; i++) {
        path = mk_path(path, incdir.dir[i], fname);
        if (i >= (*skips) && access(path, R_OK) == 0) {
            *skips = i;
            return path;
        }
    }
    return NULL;
}

static void drc_include(Token *t, int skips) {

    Token *tp = NULL;
    if ((t = consume("<"))) {
        char *ps = t->pos + 1;
        while (!token_cmp(t, ">")) {
            t = consume_any();
        }
        tp = token_new(TK_SYSTEM_SRC, ps, t->pos);
    } else {
        tp = expect_id(TK_LITERAL);
        tp->id = TK_USR_SRC;
    }

    char *path = strndup(tp->pos, tp->len);
    if (!(tp->len > 0 && *(tp->pos) == '/')) {
        path = inc_path_find(path, &skips, tp->id != TK_SYSTEM_SRC);
    }
    exit_if(!path, tp, "Can not find include file: %.*s", tp->len, tp->pos);

    env_push(path, skips);
    stmt(0);
    env_pop();
}

static void drc_define() {
    Token *key = expect_id(TK_IDENT);
    Token *params = NULL;
    if ((token_cmp(cur, "(")) && !cur->leadings) {
        params = consume_func_args();
    }
    macro_add(strndup(key->pos, key->len), params, consume_to_lnend());
}

static int primary() {
    Token *t = NULL;
    int ret = 0;
    if (consume("(")) {
        ret = expr();
        expect(")");
    } else if ((t = consume_id(TK_NUM))) {
        ret = atoi(strndup(t->pos, t->len));
    } else if ((t = consume_id(TK_CH))) {
        char *p = *t->pos == '\\' ? t->pos + 1 : t->pos;
        exit_if(*(p + 1) != '\'', t, "Invalid char length");
        ret = *p;
    } else if (consume("defined")) {
        if (consume("(")) {
            t = expect_id(TK_IDENT);
            expect(")");
        } else {
            t = expect_id(TK_IDENT);
        }
        ret = macro_get(t, cur) ? 1 : 0;
    } else if ((t = consume_id(TK_IDENT)) && macro_get(t, cur)) {
        Token *tt = expand_macro(&t);
        t->next = cur;
        cur = tt;
        ret = expr();
    }
    return ret;
}

static int unary() { return consume("!") ? !primary() : primary(); }

static int mul() {
    int ret = unary();
    while (1) {
        if (consume("+")) {
            ret += unary();
        } else if (consume("-")) {
            ret -= unary();
        } else {
            break;
        }
    }
    return ret;
}

static int plus() {
    int ret = mul();
    while (1) {
        if (consume("*")) {
            ret *= mul();
        } else if (consume("/")) {
            ret /= mul();
        } else {
            break;
        }
    }
    return ret;
}

static int shift() {
    int ret = plus();
    while (1) {
        if (consume(">>")) {
            ret >>= plus();
        } else if (consume("<<")) {
            ret <<= plus();
        } else {
            break;
        }
    }
    return ret;
}

static int relational() {
    int ret = shift();
    while (1) {
        if (consume(">")) {
            ret = ret > shift();
        } else if (consume(">=")) {
            ret = ret >= shift();
        } else if (consume("<")) {
            ret = ret < shift();
        } else if (consume("<=")) {
            ret = ret <= shift();
        } else if (consume("==")) {
            ret = ret == shift();
        } else if (consume("!=")) {
            ret = ret != shift();
        } else {
            break;
        }
    }
    return ret;
}

static int and() {
    int ret = relational();
    while (consume("&&")) {
        ret = relational() && ret;
    }
    return ret;
}

static int or() {
    int ret = and();
    while (consume("||")) {
        ret = and() || ret;
    }
    return ret;
}

static int expr() {
    int ret = or();
    if (consume("?")) {
        int ret1 = expr();
        expect(":");
        int ret2 = expr();
        return ret ? ret1 : ret2;
    }
    return ret;
}

static int ifcond() {
    int ret = expr();
    expect_id(TK_NEWLINE);
    return ret;
}

static void cntlflow(int on) {

    on ? stmt(0) : stmt_off();

    while (consume("elif")) {
        on = !on && expr();
        on ? stmt(0) : stmt_off();
    }
    if (consume("else")) {
        !on ? stmt(0) : stmt_off();
    }
    expect("endif");
}

static void stmt_off() {
    while (cur->id != TK_EOF) {
        if (consume_id(TK_DIRECTIVE)) {
            if (consume("if") || consume("ifdef") || consume("ifndef")) {
                consume_to_lnend();
                stmt_off();
                while (consume("elif")) {
                    consume_to_lnend();
                    stmt_off();
                }
                if (consume("else")) {
                    stmt_off();
                }
                expect("endif");
                continue;
            } else if (token_cmp(cur, "elif") || token_cmp(cur, "else") ||
                       token_cmp(cur, "endif")) {
                return;
            }
            continue;
        }
        consume_any();
    }
    return;
}

static void stmt(int is_top) {
    Token *t = NULL;
    while (cur->id != TK_EOF) {
        if (consume_id(TK_DIRECTIVE)) {
            if (consume("define")) {
                drc_define();
            } else if (consume("undef")) {
                macro_rm(expect_id(TK_IDENT));
            } else if (consume("warning")) {
                exit_if(2, consume_to_lnend(), "warning message");
            } else if (consume("error")) {
                exit_if(1, consume_to_lnend(), "error message");
            } else if ((t = consume("include_next"))) {
                drc_include(t, env->skips + 1);
            } else if ((t = (consume("include")))) {
                drc_include(t, 0);
            } else if (consume("if")) {
                cntlflow(ifcond());
            } else if (consume("ifdef")) {
                Token *t = consume_to_lnend();
                cntlflow(!!macro_get(t, t->next));
            } else if (consume("ifndef")) {
                Token *t = consume_to_lnend();
                cntlflow(!macro_get(t, t->next));
            } else if (token_cmp(cur, "endif") || token_cmp(cur, "elif") ||
                       token_cmp(cur, "else")) {
                exit_if(is_top, cur, "no matched if-staement");
                return;
            } else {
                exit_if(1, cur, "invalid token %.*s", cur->len, cur->pos);
            }
            continue;
        }
        if ((t = consume_id(TK_IDENT))) {
            macro_org = t;
            ocur->next = expand_macro(&t);
            ocur = t;
        } else {
            ocur = token_stitch(consume_any(), ocur);
        }
    }
    ocur = token_stitch(cur, ocur);
    return;
}

static void env_push(char *path, int skips) {
    env->pos = pos;
    env->cur = cur;
    Env *newe = calloc(sizeof(Env), 1);
    newe->path = path;
    newe->pos = newe->input = read_file(path);
    newe->skips = skips;
    newe->next = env;
    env = newe;

    pos = env->pos;
    cur = env->cur = token_instant(TK_SPACES, "");
}

static void env_pop() {
    env = env->next;
    pos = env->pos;
    cur = env->cur;
}

static void macro_predefine() {
    for (int i = 0; predefined[i].id != TK_EOF; i++) {
        Predefined pd = predefined[i];
        macro_add(pd.name, NULL, token_instant(pd.id, pd.value));
    }
}

static void print_tokens(Token *t) {
    for (; t; t = t->next) {
        if (t->leadings) {
            print_tokens(t->leadings);
        }
        if (token_cmp(t, "__LINE__")) {
            dprintf(1, "%d", linenum(t->macro_org));
        } else if (token_cmp(t, "__FILE__")) {
            dprintf(1, "\"%s\"", t->env->path);
        } else {
            char *ws = t->id == TK_LITERAL ? "\"" : t->id == TK_CH ? "'" : "";
            dprintf(1, "%s%.*s%s", ws, t->len, t->pos, ws);
        }
    }
}

static char *setopts(int ac, char **av) {
    int opt;
    int io = 0;
    while ((opt = getopt(ac, av, "I:")) != -1) {
        switch (opt) {
        case 'I':
            memmove(incdir.dir + io + 1, incdir.dir + io,
                    sizeof(char *) * incdir.len++);
            incdir.dir[io++] = optarg;
            break;
        default:
            exit_if(1, NULL, "usage: %s [-I dir] file", av[0]);
        }
    }
    exit_if(optind >= ac, NULL, "Missing file name");
    return av[optind];
}

int main(int ac, char **av) {

    env = calloc(sizeof(Env), 1);
    keywords_init();
    macro_predefine();
    char *filepath = setopts(ac, av);

    env_push(filepath, 0);
    Token head = {0};
    ocur = &head;
    stmt(1);
    env_pop();

    print_tokens(head.next);

    return 0;
}