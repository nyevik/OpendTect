#pragma once

/*
Origin: https://github.com/vivkin/gason

---------------------------------------------------

The MIT License (MIT)

Copyright (c) 2013-2015 Ivan Vashchaev

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/


#include <stdint.h>
#include <stddef.h>
#include <assert.h>

namespace Gason		// --OD added
{			// --OD added

    enum JsonTag {
	JSON_NUMBER = 0,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
	JSON_TRUE,
	JSON_FALSE,
	JSON_INUMBER,
	JSON_MIXED,
	JSON_NULL = 0xF
    };

    struct JsonNode;

#define JSON_VALUE_PAYLOAD_MASK 0x00007FFFFFFFFFFFULL
#define JSON_VALUE_NAN_MASK 0x7FF8000000000000ULL
#define JSON_VALUE_TAG_MASK 0xF
#define JSON_VALUE_TAG_SHIFT 47

    struct JsonValue {
	JsonTag tag;
	union {
	    uint64_t uval;
	    int64_t ival;
	    double fval;
	    void *pval;
	};

	JsonValue(double x)
	    : tag(JSON_NUMBER)
	    , fval(x) {
	}
	JsonValue(int64_t x)
	    : tag(JSON_INUMBER)
	    , ival(x) {
	}
	JsonValue(JsonTag atag = JSON_NULL, void *payload = nullptr)
	    : tag(atag)
	    , pval(payload) {
	}
	bool isDouble() const {
	    return tag == JSON_NUMBER;
	}
	JsonTag getTag() const {
	    return tag;
	}
	uint64_t getPayload() const {
	    assert(tag != JSON_NUMBER && tag != JSON_INUMBER);
	    return (uint64_t)(uintptr_t)pval;
	}
	double toNumber() const {
	    assert(tag == JSON_NUMBER || tag == JSON_INUMBER);
	    return tag == JSON_NUMBER ? fval : (double)ival;
	}
	int64_t toInt64() const {
	    assert(tag == JSON_NUMBER || tag == JSON_INUMBER);
	    return tag == JSON_INUMBER ? ival : (int64_t)fval;
	}
	char *toString() const {
	    assert( getTag() == JSON_STRING );
	    return (char *)pval;
	}
	JsonNode *toNode() const {
	    assert(getTag() == JSON_ARRAY || getTag() == JSON_OBJECT);
	    return (JsonNode *)pval;
	}
    };

    struct JsonNode {
	JsonValue value;
	JsonNode *next;
	char *key;
    };

    struct JsonIterator {
	JsonNode *p;

	void operator++() {
	    p = p->next;
	}
	bool operator!=(const JsonIterator &x) const {
	    return p != x.p;
	}
	JsonNode *operator*() const {
	    return p;
	}
	JsonNode *operator->() const {
	    return p;
	}
    };

    inline JsonIterator begin(JsonValue o) {
	return JsonIterator{ o.toNode() };
    }
    inline JsonIterator end(JsonValue) {
	return JsonIterator{nullptr};
    }

#define JSON_ERRNO_MAP(XX)                           \
    XX(OK, "ok")                                     \
    XX(BAD_NUMBER, "bad number")                     \
    XX(BAD_STRING, "bad string")                     \
    XX(BAD_IDENTIFIER, "bad identifier")             \
    XX(STACK_OVERFLOW, "stack overflow")             \
    XX(STACK_UNDERFLOW, "stack underflow")           \
    XX(MISMATCH_BRACKET, "mismatch bracket")         \
    XX(UNEXPECTED_CHARACTER, "unexpected character") \
    XX(UNQUOTED_KEY, "unquoted key")                 \
    XX(BREAKING_BAD, "breaking bad")                 \
    XX(ALLOCATION_FAILURE, "allocation failure")

    enum JsonErrno {
#define XX(no, str) JSON_##no,
	JSON_ERRNO_MAP(XX)
#undef XX
    };

    const char *jsonStrError(int err);

    class JsonAllocator {
	struct Zone {
	    Zone *next;
	    size_t used;
	} *head;

    public:
	JsonAllocator() : head(nullptr) {};
	JsonAllocator(const JsonAllocator &) = delete;
	JsonAllocator &operator=(const JsonAllocator &) = delete;
	JsonAllocator(JsonAllocator &&x) : head(x.head) {
	    x.head = nullptr;
	}
	JsonAllocator &operator=(JsonAllocator &&x) {
	    head = x.head;
	    x.head = nullptr;
	    return *this;
	}
	~JsonAllocator() {
	    deallocate();
	}
	void *allocate(size_t size);
	void deallocate();
    };

    int jsonParse( char *str, char **endptr, JsonValue *value,
		   JsonAllocator &allocator );

}   // --OD added
