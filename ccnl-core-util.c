/*
 * @f ccnl-core-util.c
 * @b CCN lite, common utility procedures (used by utils as well as relays)
 *
 * Copyright (C) 2011-14, Christian Tschudin, University of Basel
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * File history:
 * 2014-06-18 created
 */


struct ccnl_prefix_s*
ccnl_prefix_new(int suite, int cnt)
{
    struct ccnl_prefix_s *p;

    p = (struct ccnl_prefix_s *) ccnl_calloc(1, sizeof(struct ccnl_prefix_s));
    if (!p)
        return NULL;
    p->comp = (unsigned char**) ccnl_malloc(cnt * sizeof(unsigned char*));
    p->complen = (int*) ccnl_malloc(cnt * sizeof(int));
    if (!p->comp || !p->complen) {
        free_prefix(p);
        return NULL;
    }
    p->compcnt = 0;
    p->suite = suite;
    p->chunknum = -1;

    return p;
}

int
hex2int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 0x0a;
    return 0;
}

int
unescape_component(char *comp) // inplace, returns len after shrinking
{
    char *in = comp, *out = comp;
    int len;

    for (len = 0; *in; len++) {
        if (in[0] != '%' || !in[1] || !in[2]) {
            *out++ = *in++;
            continue;
        }
        *out++ = hex2int(in[1])*16 + hex2int(in[2]);
        in += 3;
    }
    return len;
}

int
ccnl_pkt_mkComponent(int suite, unsigned char *dst, char *src)
{
    int len = 0;

//    printf("ccnl_pkt_mkComponent(%d, %s)\n", suite, src);

    switch (suite) {
#ifdef USE_SUITE_CCNTLV
    case CCNL_SUITE_CCNTLV:
    {
        unsigned short *sp = (unsigned short*) dst;
        *sp++ = htons(CCNX_TLV_N_NameSegment);
        len = strlen(src);
        *sp++ = htons(len);
        memcpy(sp, src, len);
        len += 2*sizeof(unsigned short);
        break;
    }
#endif
    default:
        len = strlen(src);
        memcpy(dst, src, len);
        break;
    }
    return len;
}

int
ccnl_pkt_prependComponent(int suite, char *src, int *offset, unsigned char *buf)
{
    int len = strlen(src);

//    DEBUGMSG(99, "ccnl_pkt_prependComponent(%d, %s)\n", suite, src);

    if (*offset < len)
        return -1;
    memcpy(buf + *offset - len, src, len);
    *offset -= len;

#ifdef USE_SUITE_CCNTLV
    if (suite == CCNL_SUITE_CCNTLV) {
        unsigned short *sp = (unsigned short*) (buf + *offset) - 1;
        if (*offset < 4)
            return -1;
        *sp-- = htons(len);
        *sp = htons(CCNX_TLV_N_NameSegment);
        len += 2*sizeof(unsigned short);
        *offset -= 2*sizeof(unsigned short);
    }
#endif

    return len;
}

// fill in the compVector (watch out: this modifies the uri string)
int
ccnl_URItoComponents(char **compVector, char *uri)
{
    int i, len;

    if (*uri == '/')
        uri++;

    for (i = 0; *uri && i < (CCNL_MAX_NAME_COMP - 1); i++) {
        compVector[i] = uri;
        while (*uri && *uri != '/')
            uri++;
        if (*uri) {
            *uri = '\0';
            uri++;
        }
        len = unescape_component(compVector[i]);
        compVector[i][len] = '\0';
    }
    compVector[i] = NULL;

    return i;
}

// turn an URI into an internal prefix (watch out: this modifies the uri string)
struct ccnl_prefix_s *
ccnl_URItoPrefix(char* uri, int suite, char *nfnexpr, int *chunknum)
{
    struct ccnl_prefix_s *p;
    char *compvect[CCNL_MAX_NAME_COMP];
    int cnt, i, len = 0;

    DEBUGMSG(99, "ccnl_URItoPrefix(suite=%d, uri=%s, nfn=%s)\n",
             suite, uri, nfnexpr);

    if (strlen(uri))
        cnt = ccnl_URItoComponents(compvect, uri);
    else
        cnt = 0;
    if (nfnexpr && *nfnexpr)
        cnt += 1;

    p = ccnl_prefix_new(suite, cnt);
    if (!p)
        return NULL;

    for (i = 0, len = 0; i < cnt; i++) {
        if (i == (cnt-1) && nfnexpr && *nfnexpr)
            len += strlen(nfnexpr);
        else
            len += strlen(compvect[i]);
    }
#ifdef USE_SUITE_CCNTLV
    if (suite == CCNL_SUITE_CCNTLV)
        len += cnt * 4; // add TL size
#endif
    
    p->bytes = ccnl_malloc(len);
    if (!p->bytes) {
        free_prefix(p);
        return NULL;
    }
    for (i = 0, len = 0; i < cnt; i++) {
        char *cp = (i == (cnt-1) && nfnexpr && *nfnexpr) ?
                                              nfnexpr : (char*) compvect[i];
        p->comp[i] = p->bytes + len;
        p->complen[i] = ccnl_pkt_mkComponent(suite, p->comp[i], cp);
        len += p->complen[i];
    }

    p->compcnt = cnt;
#ifdef USE_NFN
    if (nfnexpr && *nfnexpr)
        p->nfnflags |= CCNL_PREFIX_NFN;
#endif
    p->chunknum = chunknum ? *chunknum : -1;

    return p;
}

struct ccnl_prefix_s*
ccnl_prefix_dup(struct ccnl_prefix_s *prefix)
{
    int i = 0, len;
    struct ccnl_prefix_s *p;

    p = ccnl_prefix_new(prefix->suite, prefix->compcnt);
    if (!p)
        return p;

    p->compcnt = prefix->compcnt;
#ifdef USE_NFN
    p->nfnflags = prefix->nfnflags;
#endif
    p->chunknum = prefix->chunknum;

    for (i = 0, len = 0; i < prefix->compcnt; i++)
        len += prefix->complen[i];
    p->bytes = ccnl_malloc(len);
    if (!p->bytes) {
        free_prefix(p);
        return NULL;
    }
    
    for (i = 0, len = 0; i < prefix->compcnt; i++) {
        p->complen[i] = prefix->complen[i];
        p->comp[i] = p->bytes + len;
        memcpy(p->bytes + len, prefix->comp[i], p->complen[i]);
        len += p->complen[i];
    }

    return p;
}

// ----------------------------------------------------------------------

int
ccnl_pkt2suite(unsigned char *data, int len)
{

    if (len <= 0) 
        return -1;

#ifdef USE_SUITE_CCNB
    if (*data == 0x01 || *data == 0x04)
        return CCNL_SUITE_CCNB;
#endif

#ifdef USE_SUITE_CCNTLV
    if (data[0] == CCNX_TLV_V0 && len > 1) {
        if (data[1] == CCNX_PT_Interest ||
            data[1] == CCNX_PT_ContentObject) 
            return CCNL_SUITE_CCNTLV;
    } 
#endif

#ifdef USE_SUITE_NDNTLV
    if (*data == NDN_TLV_Interest || *data == NDN_TLV_Data)
        return CCNL_SUITE_NDNTLV;
#endif

#ifdef USE_SUITE_LOCALRPC
    if (*data == 0x80)
        return CCNL_SUITE_LOCALRPC;
#endif
    return -1;
}

// ----------------------------------------------------------------------

char*
ccnl_addr2ascii(sockunion *su)
{
    static char result[130];

    switch (su->sa.sa_family) {
#ifdef USE_ETHERNET
    case AF_PACKET:
    {
        struct sockaddr_ll *ll = &su->eth;
        strcpy(result, eth2ascii(ll->sll_addr));
        sprintf(result+strlen(result), "/0x%04x",
            ntohs(ll->sll_protocol));
        return result;
    }
#endif
    case AF_INET:
        sprintf(result, "%s/%d", inet_ntoa(su->ip4.sin_addr),
                ntohs(su->ip4.sin_port));
        return result;
#ifdef USE_UNIXSOCKET
    case AF_UNIX:
        strcpy(result, su->ux.sun_path);
        return result;
#endif
    default:
        break;
    }
    return NULL;
}

// ----------------------------------------------------------------------

#ifndef CCNL_LINUXKERNEL

static char *prefix_buf1;
static char *prefix_buf2;
static char *buf;

char*
ccnl_prefix_to_path(struct ccnl_prefix_s *pr)
{
    int len = 0, i;

    if (!pr)
        return NULL;

    if (!buf) {
        struct ccnl_buf_s *b;
        b = ccnl_buf_new(NULL, 2048);
        ccnl_core_addToCleanup(b);
        prefix_buf1 = (char*) b->data;
        b = ccnl_buf_new(NULL, 2048);
        ccnl_core_addToCleanup(b);
        prefix_buf2 = (char*) b->data;
        buf = prefix_buf1;
    } else if (buf == prefix_buf2)
        buf = prefix_buf1;
    else
        buf = prefix_buf2;

#ifdef USE_NFN
    if (pr->nfnflags & CCNL_PREFIX_NFN)
        len += sprintf(buf + len, "nfn");
    if (pr->nfnflags & CCNL_PREFIX_THUNK)
        len += sprintf(buf + len, "thunk");
    if (pr->nfnflags)
        len += sprintf(buf + len, "[");
#endif

    for (i = 0; i < pr->compcnt; i++) {
        int skip = 0;

#ifdef USE_SUITE_CCNTLV
        if (pr->suite == CCNL_SUITE_CCNTLV)
            skip = 4;
/*
            len += sprintf(buf + len, "/%%x%02x%02x%.*s",
                           pr->comp[i][0], pr->comp[i][1],
                           pr->complen[i]-4, pr->comp[i]+4);
*/
#endif

#ifdef USE_NFN
        if (pr->compcnt == 1 && (pr->nfnflags & CCNL_PREFIX_NFN) &&
            !strncmp("call", (char*)pr->comp[i] + skip, 4)) {
            len += sprintf(buf + len, "%.*s",
                           pr->complen[i]-skip, pr->comp[i]+skip);
        } else
#endif
            len += sprintf(buf + len, "/%.*s",
                           pr->complen[i]-skip, pr->comp[i]+skip);
    }

#ifdef USE_NFN
    if (pr->nfnflags)
        len += sprintf(buf + len, "]");
#endif

    buf[len] = '\0';

    return buf;
}

struct ccnl_buf_s*
ccnl_mkSimpleInterest(struct ccnl_prefix_s *name, int *nonce)
{
    struct ccnl_buf_s *buf = NULL;
    unsigned char *tmp;
    int len = 0, offs;

    tmp = ccnl_malloc(CCNL_MAX_PACKET_SIZE);
    offs = CCNL_MAX_PACKET_SIZE;

    switch (name->suite) {
#ifdef USE_SUITE_CCNB
    case CCNL_SUITE_CCNB:
        len = ccnl_ccnb_fillInterest(name, NULL, tmp, CCNL_MAX_PACKET_SIZE);
        offs = 0;
        break;
#endif
#ifdef USE_SUITE_CCNTLV
    case CCNL_SUITE_CCNTLV:
        len = ccnl_ccntlv_prependInterestWithHdr(name, &offs, tmp);
        break;
#endif
#ifdef USE_SUITE_NDNTLV
    case CCNL_SUITE_NDNTLV:
        len = ccnl_ndntlv_prependInterest(name, -1, NULL, &offs, tmp);
        break;
#endif
    default:
        break;
    }

    if (len)
        buf = ccnl_buf_new(tmp + offs, len);
    ccnl_free(tmp);

    return buf;
}

struct ccnl_buf_s*
ccnl_mkSimpleContent(struct ccnl_prefix_s *name,
                     unsigned char *payload, int paylen, int *payoffset)
{
    struct ccnl_buf_s *buf = NULL;
    unsigned char *tmp;
    int len = 0, contentpos = 0, offs;

    tmp = ccnl_malloc(CCNL_MAX_PACKET_SIZE);
    offs = CCNL_MAX_PACKET_SIZE;

    switch (name->suite) {
#ifdef USE_SUITE_CCNB
    case CCNL_SUITE_CCNB:
        len = ccnl_ccnb_fillContent(name, payload, paylen, &contentpos, tmp);
        offs = 0;
        break;
#endif
#ifdef USE_SUITE_CCNTLV
    case CCNL_SUITE_CCNTLV:
        len = ccnl_ccntlv_prependContentWithHdr(name, payload, paylen, 
                                                NULL, // lastchunknum
                                                &offs, &contentpos, tmp);
        break;
#endif
#ifdef USE_SUITE_NDNTLV
    case CCNL_SUITE_NDNTLV:
        len = ccnl_ndntlv_prependContent(name, payload, paylen,
                                         &offs, &contentpos, NULL, 0, tmp);
        break;
#endif
    default:
        break;
    }

    if (len) {
        buf = ccnl_buf_new(tmp + offs, len);
        if (payoffset)
            *payoffset = contentpos;
    }
    ccnl_free(tmp);

    return buf;
}

#endif // CCNL_LINUXKERNEL

// ----------------------------------------------------------------------

#define LAMBDACHAR '@'

#define term_is_var(t)     (!(t)->m)
#define term_is_app(t)     ((t)->m && (t)->n)
#define term_is_lambda(t)  (!(t)->n)

char*
ccnl_lambdaParseVar(char **cpp)
{
    char *p;
    int len;

    p = *cpp;
    if (*p && *p == '\'')
        p++;
    while (*p && (isalnum(*p) || *p == '_' || *p == '=' || *p == '/' || *p == '.'))
	   p++;
    len = p - *cpp;
    p = ccnl_malloc(len+1);
    if (!p)
        return 0;
    memcpy(p, *cpp, len);
    p[len] = '\0';
    *cpp += len;
    return p;
}

struct ccnl_lambdaTerm_s*
ccnl_lambdaStrToTerm(int lev, char **cp, int (*prt)(char* fmt, ...))
{
/* t = (v, m, n)

   var:     v!=0, m=0, n=0
   app:     v=0, m=f, n=arg
   lambda:  v!=0, m=body, n=0
 */
    struct ccnl_lambdaTerm_s *t = 0, *s, *u;

    while (**cp) {
        while (isspace(**cp))
            *cp += 1;

    //  myprintf(stderr, "parseKRIVINE %d %s\n", lev, *cp);

        if (**cp == ')')
            return t;
        if (**cp == '(') {
            *cp += 1;
            s = ccnl_lambdaStrToTerm(lev+1, cp, prt);
            if (!s)
                return 0;
            if (**cp != ')') {
                if (prt) {
                    prt("parseKRIVINE error: missing )\n");
                }
                return 0;
            } else {
                *cp += 1;
            }
        } else if (**cp == LAMBDACHAR) {
            *cp += 1;
            s = ccnl_calloc(1, sizeof(*s));
            s->v = ccnl_lambdaParseVar(cp);
            s->m = ccnl_lambdaStrToTerm(lev+1, cp, prt);
    //      printKRIVINE(dummybuf, s->m, 0);
    //      printf("  after lambda: /%s %s --> <%s>\n", s->v, dummybuf, *cp);
        } else {
            s = ccnl_calloc(1, sizeof(*s));
            s->v = ccnl_lambdaParseVar(cp);
    //      printf("  var: <%s>\n", s->v);
        }
        if (t) {
    //      printKRIVINE(dummybuf, t, 0);
    //      printf("  old term: <%s>\n", dummybuf);
            u = ccnl_calloc(1, sizeof(*u));
            u->m = t;
            u->n = s;
            t = u;
        } else {
            t = s;
        }
//  printKRIVINE(dummybuf, t, 0);
//  printf("  new term: <%s>\n", dummybuf);
    }
//    printKRIVINE(dummybuf, t, 0);
//    printf("  we return <%s>\n", dummybuf);
    return t;
}

int
ccnl_lambdaTermToStr(char *cfg, struct ccnl_lambdaTerm_s *t, char last)
{
    int len = 0;

    if (t->v && t->m) { // Lambda (sequence)
        len += sprintf(cfg + len, "(%c%s", LAMBDACHAR, t->v);
        len += ccnl_lambdaTermToStr(cfg + len, t->m, 'a');
        len += sprintf(cfg + len, ")");
        return len;
    }
    if (t->v) { // (single) variable
        if (isalnum(last))
            len += sprintf(cfg + len, " %s", t->v);
        else
            len += sprintf(cfg + len, "%s", t->v);
        return len;
    }
    // application (sequence)
#ifdef CORRECT_PARENTHESES
    len += sprintf(cfg + len, "(");
    len += printKRIVINE(cfg + len, t->m, '(');
    len += printKRIVINE(cfg + len, t->n, 'a');
    len += sprintf(cfg + len, ")");
#else
    if (t->n->v && !t->n->m) {
        len += ccnl_lambdaTermToStr(cfg + len, t->m, last);
        len += ccnl_lambdaTermToStr(cfg + len, t->n, 'a');
    } else {
        len += ccnl_lambdaTermToStr(cfg + len, t->m, last);
        len += sprintf(cfg + len, " (");
        len += ccnl_lambdaTermToStr(cfg + len, t->n, '(');
        len += sprintf(cfg + len, ")");
    }
#endif
    return len;
}

void
ccnl_lambdaFreeTerm(struct ccnl_lambdaTerm_s *t)
{
    if (t) {
        ccnl_free(t->v);
        ccnl_lambdaFreeTerm(t->m);
        ccnl_lambdaFreeTerm(t->n);
        ccnl_free(t);
    }
}

int
ccnl_lambdaStrToComponents(char **compVector, char *str)
{
    return ccnl_URItoComponents(compVector, str);
}

// ----------------------------------------------------------------------

int
ccnl_str2suite(char *cp)
{
#ifdef USE_SUITE_CCNB
    if (!strcmp(cp, "ccnb"))
        return CCNL_SUITE_CCNB;
#endif
#ifdef USE_SUITE_CCNTLV
    if (!strcmp(cp, "ccnx2014"))
        return CCNL_SUITE_CCNTLV;
#endif
#ifdef USE_SUITE_NDNTLV
    if (!strcmp(cp, "ndn2013"))
        return CCNL_SUITE_NDNTLV;
#endif
    return -1;
}

char*
ccnl_suite2str(int suite)
{
#ifdef USE_SUITE_CCNB
    if (suite == CCNL_SUITE_CCNB)
        return "ccnb";
#endif
#ifdef USE_SUITE_CCNTLV
    if (suite == CCNL_SUITE_CCNTLV)
        return "ccnx2014";
#endif
#ifdef USE_SUITE_NDNTLV
    if (suite == CCNL_SUITE_NDNTLV)
        return "ndn2013";
#endif
    return "?";
}

// eof