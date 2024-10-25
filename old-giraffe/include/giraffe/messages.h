#ifndef GIRAFFE_MESSAGES_H_
#define GIRAFFE_MESSAGES_H_

// #define O(enum_name, padded_number, id, name, ...) GF_MESSAGE_##padded_number##_##enum_name
#define GF_MESSAGE_IDS_(O, S) \
	O(UNKNOWN, 0000, 0, "unknown") S \
	O(INVALID_CHARACTER, 0001, 1, "invalid character") S \
	O(LAST, 9999, 9999, "last")

#endif // GIRAFFE_MESSAGES_H_
