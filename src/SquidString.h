/*
 * $Id: SquidString.h,v 1.16 2008/02/11 23:01:23 amosjeffries Exp $
 *
 * DEBUG: section 67    String
 * AUTHOR: Duane Wessels
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#ifndef SQUID_STRING_H
#define SQUID_STRING_H

#include "config.h"

/* forward decls */

class CacheManager;

/** todo checks to wrap this include properly */
#include <ostream>


#define DEBUGSTRINGS 0
#if DEBUGSTRINGS
#include "splay.h"

class String;

class StringRegistry
{

public:
    static StringRegistry &Instance();

    void add
        (String const *);

    void registerWithCacheManager(void);

    void remove
        (String const *);

private:
    static OBJH Stat;

    static StringRegistry Instance_;

    static SplayNode<String const *>::SPLAYWALKEE Stater;

    Splay<String const *> entries;

    bool registered;

};

class StoreEntry;
#endif

class String
{

public:
    _SQUID_INLINE_ String();
    String (char const *);
    String (String const &);
    ~String();

    String &operator =(char const *);
    String &operator =(String const &);
    bool operator ==(String const &) const;
    bool operator !=(String const &) const;

    /**
     * Retrieve a single character in the string.
     \param pos	Position of character to retrieve.
     */
    _SQUID_INLINE_ char &operator [](unsigned int pos);

    _SQUID_INLINE_ int size() const;
    _SQUID_INLINE_ char const * buf() const;
    void limitInit(const char *str, int len); // TODO: rename to assign()
    void clean();
    void reset(char const *str);
    void append(char const *buf, int len);
    void append(char const *buf);
    void append(char const);
    void append (String const &);
    void absorb(String &old);
    _SQUID_INLINE_ const char * pos(char const *) const;
    _SQUID_INLINE_ const char * pos(char const ch) const;
    _SQUID_INLINE_ const char * rpos(char const ch) const;
    _SQUID_INLINE_ int cmp (char const *) const;
    _SQUID_INLINE_ int cmp (char const *, size_t count) const;
    _SQUID_INLINE_ int cmp (String const &) const;
    _SQUID_INLINE_ int caseCmp (char const *) const;
    _SQUID_INLINE_ int caseCmp (char const *, size_t count) const;

    /** \deprecated Use assignment to [] position instead.
     *              ie   str[0] = 'h';
     */
    _SQUID_INLINE_ void set(char const *loc, char const ch);

    /** \deprecated Use assignment to [] position instead.
     *              ie   str[newLength] = '\0';
     */
    _SQUID_INLINE_ void cut(size_t newLength);

    /** \deprecated Use assignment to [] position instead.
     *              ie   str[newLength] = '\0';
     */
    _SQUID_INLINE_ void cutPointer(char const *loc);

#if DEBUGSTRINGS

    void stat (StoreEntry *) const;

#endif



private:
    void allocAndFill(const char *str, int len);
    void allocBuffer(size_t sz);
    void setBuffer(char *buf, size_t sz);

    /* never reference these directly! */
    unsigned short int size_; /* buffer size; 64K limit */

    unsigned short int len_;  /* current length  */

    char *buf_;
};

_SQUID_INLINE_ std::ostream & operator<<(std::ostream& os, String const &aString);

#ifdef _USE_INLINE_
#include "String.cci"
#endif

#endif /* SQUID_STRING_H */
