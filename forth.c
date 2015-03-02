/** @file           forth.c
 *  @brief          A small FORTH interpreter library based on a previous
 *                  IOCCC winner circa 1992, 'buzzard.2.c'.
 *  @author         Richard James Howe.
 *  @copyright      Copyright 2015 Richard James Howe.
 *  @license        LGPL v2.1 or later version
 *  @email          howe.r.j.89@gmail.com
 *
 *  @todo           Documentation of the internals.
 *  @todo           Rewrite word header to be more compact, include the
 *                  word name in the header and not a separate place and
 *                  to have a 'hide' field.
 *  @todo           Allow input from a string for an eval() function.
 */
#include "forth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WARN(MSG) \
        fprintf(stderr,"( error \"%s\" %s %d )\n", (MSG), __FILE__, __LINE__)
#define CORESZ    ((UINT16_MAX + 1) / 2)
#define STROFF    (CORESZ/2)
#define STKSZ     (CORESZ/64)
#define BLKSZ     (1024u)
#define CK(X)     ((X) & 0x7fff)

struct forth_obj {
        FILE *in, *out;
        uint8_t *s; /*string store pointer*/
        uint16_t *S, L, I, t, w, f, x, m[CORESZ];
        unsigned invalid :1; /*invalidate this object if true*/
};

enum registers { DICTIONARY = 0, RSTK = 1, STATE = 8, HEX = 9 };

enum codes { PUSH, COMPILE, RUN, DEFINE, IMMEDIATE, COMMENT, READ, LOAD, 
STORE, SUB, ADD, MUL, DIV, LESS, EXIT, EMIT, KEY, FROMR, TOR, JMP, JMPZ, 
PNUM, QUOTE, COMMA, EQUAL, SWAP, DUP, DROP, TAIL, BSAVE, BLOAD, LAST }; 

static char *names[] = { "read", "@", "!", "-", "+", "*", "/", "<", "exit", 
"emit", "key", "r>", ">r", "j",  "jz", ".", "'", ",", "=", "swap", "dup",
"drop", "tail", "save", "load", NULL }; 

static int compile_word(forth_obj_t * o, uint16_t code, char *str)
{
        uint16_t *m;
        int r = 0;
        m = o->m;
        m[m[0]++] = o->L;
        o->L = *m - 1;
        m[m[0]++] = o->t;
        m[m[0]++] = code;
        if (str) strcpy((char *)o->s + o->t, str);
        else     r = fscanf(o->in, "%31s", o->s + o->t);
        o->t += strlen((char*)o->s + o->t) + 1;
        return r;
}

static int blockio(void *p, uint16_t poffset, uint16_t id, char rw)
{
        char name[16]; /* XXXX + ".blk" + '\0' + a little spare change */
        FILE *file;
        size_t n;
        if((poffset > (CORESZ - BLKSZ)) || !(rw == 'w' || rw == 'r'))
                return WARN("invalid address or mode"), -1;
        sprintf(name, "%04x.blk", (int)id);
        if(!(file = fopen(name, rw == 'r' ? "rb" : "wb")))
                return WARN("could not open file"), -1;
        n = rw == 'w' ? 
                fwrite(p+poffset, 1, BLKSZ, file) : 
                fread(p+poffset, 1, BLKSZ, file);
        fclose(file);
        return n == BLKSZ ? 0 : -1;
}

static int isnum(char *s) 
{ 
        s += *s == '-' ? 1 : 0; 
        return s[strspn(s,"0123456789")] == '\0'; 
}

static uint16_t find(forth_obj_t * o) 
{
        uint16_t *m = o->m, w;
        for (w = o->L; strcmp((char*)o->s, (char*)&o->s[m[w + 1]]); w = m[w]);
        return w;
}

void forth_seti(forth_obj_t * o, FILE * in)  { o->in  = in;  }
void forth_seto(forth_obj_t * o, FILE * out) { o->out = out; }

int forth_coredump(forth_obj_t * o, FILE * dump)
{ 
        if(!o || !dump) return -1;
        return sizeof(*o) == fwrite(o, 1, sizeof(*o), dump);
}

forth_obj_t *forth_init(FILE * in, FILE * out)
{
        uint16_t *m, i, w;
        forth_obj_t *o;

        if(!in || !out || !(o = calloc(1, sizeof(*o))))
                return NULL;
        m = o->m;
        o->s = (uint8_t*) m + STROFF; /*string store offset into CORE*/
        o->in = in;
        o->out = out;

        m[0] = 32;   /*initial dictionary offset, skip registers*/
        o->L = 1; 
        o->t = 32;   /*offset into str storage defines maximum word length*/

        w = *m;
        m[m[0]++] = READ; /*create a special word that reads in*/
        m[m[0]++] = RUN;  /*call the special word recursively*/
        o->I = *m;
        m[m[0]++] = w;
        m[m[0]++] = o->I - 1;

        compile_word(o, DEFINE,    ":"); 
        compile_word(o, IMMEDIATE, "immediate"); 
        compile_word(o, COMMENT,   "#"); 
        for(i = 0, w = READ; names[i]; i++) /*compile the rest */
                compile_word(o, COMPILE, names[i]), m[m[0]++] = w++;
        m[RSTK] = CORESZ - STKSZ;            
        o->S = m + CORESZ - (2*STKSZ); 
        return o;
}

int forth_run(forth_obj_t * o)
{
        int c;
        uint16_t *m, x, *S, I, f, w;

        if(!o || o->invalid) 
		return WARN("invalid obj"), -(o->invalid = 1);
        m = o->m, x = o->x, S = o->S, I = o->I, f = o->f;

        for(;(x = m[I++]);) { 
        INNER:  switch (m[x++]) { 
                case PUSH:    *++S = f;     f = m[I++];     break;
                case COMPILE: m[m[0]++] = x;                break;
                case RUN:     m[++m[RSTK]] = I; I = x;      break;
                case DEFINE:  m[STATE] = 1;
                              if(compile_word(o, COMPILE, NULL) < 0)
                                      return -(o->invalid = 1);
                              m[m[0]++] = RUN;
                              break;
                case IMMEDIATE: *m -= 2; m[m[0]++] = RUN;   break;
                case COMMENT: while((c=fgetc(o->in)) > 0)
                                      if(c == '\n')
                                              break;
                              break;
                case READ:
                        m[RSTK]--;
                        if(fscanf(o->in, "%31s", o->s) < 1)
                                return 0;
                        w = find(o);
                        if (w - 1) {
                                x = w + 2;
                                if (!m[STATE] && m[x] == COMPILE)
                                        x++;
                                goto INNER;
                        } else if(!isnum((char*)o->s)) {
                                WARN("not a word or number");
                                break;
                        }
                        if (m[STATE]) { /*must be a number then*/
                                m[m[0]++] = 2; /*fake word push at m[2]*/
                                m[m[0]++] = strtol((char*)o->s, NULL, 0);
                        } else {
                                *++S = f;
                                f = strtol((char*)o->s, NULL, 0);
                        }
                        break;
                case LOAD:  f = m[CK(f)];                   break; 
                case STORE: m[CK(f)] = *S--; f = *S--;      break; 
                case SUB:   f = *S-- - f;                   break;
                case ADD:   f = *S-- + f;                   break;
                case MUL:   f *= *S--;                      break;
                case DIV:   f = f ? *S--/f:WARN("div 0"),0; break;
                case LESS:  f = *S-- > f;                   break;
                case EXIT:  I = m[m[RSTK]--];               break;
                case EMIT:  fputc(f, o->out); f = *S--;     break; 
                case KEY:   *++S = f; f = fgetc(o->in);     break;
                case FROMR: *++S = f; f = m[m[RSTK]--];     break;
                case TOR:   m[++m[RSTK]] = f; f = *S--;     break;
                case JMP:   I += m[I];                      break;
                case JMPZ:  I += f == 0 ? m[I]:1; f = *S--; break;
                case PNUM:  fprintf(o->out, m[HEX]? "%X":"%u", f); 
                            f = *S--;
                            break; /*should report i/o err*/
                case QUOTE: *++S = f;      f = m[I++];      break;
                case COMMA: m[m[0]++] = f; f = *S--;        break;
                case EQUAL: f = *S-- == f;                  break;
                case SWAP:  w = f;  f = *S--;   *++S = w;   break;
                case DUP:   *++S = f;                       break;
                case DROP:  f = *S--;                       break;
                case TAIL:  m[RSTK]--;                      break;
                case BSAVE: f = blockio(m,*S--,f,'w');      break;
                case BLOAD: f = blockio(m,*S--,f,'r');      break;
                default:    WARN("unknown instruction");
                            return -(o->invalid = 1);
                }
        }
        return 0; /*is 'o' still valid?*/
}

