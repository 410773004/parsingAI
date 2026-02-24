//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#ifndef _STRING_H_
#define _STRING_H_

/*!
 * \file string.h
 * @brief interface of string operation
 * \addtogroup utility
 * @{
 */

/*!
 * @brief copy string
 *
 * @param dest	pointer to destination string
 * @param src	pointer to source string
 *
 * @return	pointer to destination string
 */
extern char * strcpy(char *, const char *);

/*!
 * @brief copy a string
 *
 * @param dest		pointer to destination string
 * @param src		pointer to source string
 * @param count		number of characters to be copied from source
 *
 * @return 		pointer to destination string
 */
extern char * strncpy(char *, const char *, unsigned long);

/*!
 * @brief concatenate two strings
 *
 * @param dest	pointer to the destination array, which should contain a C string
 * @param src	string to be appended
 *
 * @return	pointer to the resulting string
 */
extern char *strcat(char *, const char *);

/*!
 * @brief concatenate two strings
 *
 * @param dest		pointer to the destination array, which should contain a C string
 * @param src		string to be appended
 * @param count		maximum number of characters to be appended
 *
 * @return		pointer to the resulting string
 */
extern char *strncat(char *, const char *, unsigned long);

/*!
 * @brief located charactor in string
 *
 * @param s	string to be scanned
 * @param c	character to be searched in str
 *
 * @return	Return a pointer to the first occurrence of the character c in the string str,
 * 		or NULL if the character is not found
 */
extern char * strchr(const char *, int);

/*!
 * @brief search a string for any of a set of bytes
 *
 * @param cs	string to be scanned
 * @param ct	string containing the characters to match
 *
 * @return	Returns a pointer to the character in @cs that matches one of the characters in @ct,
 * 		or NULL if no such character is found
 */
extern char * strpbrk(const char *, const char *);

/*!
 * @brief extract tokens from strings
 *
 * @param s	contents of this string are modified and broken into smaller strings (tokens)
 * @param ct	string containing the delimiters
 *
 * @return	Returns a pointer to the last token found in the string.
 * 		A null pointer is returned if there are no tokens left to retrieve.
 */
extern char * strtok(char *, const char *);

/*!
 * @brief locate a substring
 *
 * @param s1	string to be scanned
 * @param s2	string to be searched with-in @s1
 *
 * @return 	Return a pointer to the first occurrence in haystack of any of the entire sequence of characters specified in needle,
 * 		or a null pointer if the sequence is not present in haystack
 */
extern char * strstr(const char *, const char *);

/*!
 * @brief calculate the length of a string
 *
 * @param s	string whose length is to be found
 *
 * @return	length of string
 */
extern unsigned long strlen(const char *);

/*!
 * @brief calculate the length of a string
 *
 * @param s		string whose length is to be found
 * @param count		maximum length
 *
 * @return		length of string
 */
extern unsigned long strnlen(const char *, unsigned long);

/*!
 * @brief get length of a prefix substring
 *
 * @param s		string to be scanned
 * @param accept	string containing the list of characters to match in @s
 *
 * @return		returns the number of characters in the initial segment of @s
 * 			which consist only of characters from @accept.
 */
extern unsigned long strspn(const char *, const char *);

/*!
 * @brief compare two strings
 *
 * @param cs	first string to be compared
 * @param ct	second string to be compared
 *
 * @return	Return value < 0 if @cs was less than @ct.
 * 		Return value > 0 if @ct was less than @cs.
 * 		Return value = 0 if @cs was equal to @ct.
 */
extern int strcmp(const char *, const char *);

/*!
 * @brief compare two strings
 *
 * @param cs		first string to be compared
 * @param ct		second string to be compared
 * @param count		maximum number of characters to be compared
 *
 * @return		Return value < 0 if @cs was less than @ct.
 * 			Return value > 0 if @ct was less than @cs.
 * 			Return value = 0 if @cs was equal to @ct.
 */
extern int strncmp(const char *, const char *, unsigned long);

/*!
 * @brief converts the initial part of the string in nptr to an integer
 *
 * unsigned long int value according to the given base
 *
 * @param cp	string containing the representation of an unsigned integral number
 * @param endp	reference to an object of type char*, whose value is set by the function to the next character in str after the numerical value
 * @param base 	base of integer
 *
 * @return 	the converted value
 */
extern unsigned long strtoul(const char *cp, char **endp, unsigned int base);
extern unsigned long long strtoull(const char *cp, char **endp, unsigned int base);
extern signed long strtol(const char *cp, char **endp, unsigned int base);

/*!
 * @brief convert a string to an integer
 *
 * @param cp	string representation of an integral number
 *
 * @return	the converted value
 */
extern int atoi(const char *cp);

extern int intodec(char * dest, signed int arg, unsigned short places, unsigned int base);

/*!
 * @brief fill memmory with a constant byte
 *
 * @param s		pointer to the block of memory to fill
 * @param c		value to be set
 * @param count		number of bytes to be set to the value
 *
 * @return		Return a pointer to the memory area str
 */
extern void * memset(void *, int, unsigned int);

/*!
 * @brief copy byte sequence
 *
 * @param src		string that will be copied
 * @param dest		existing array into which you want to copy the string.
 * @param count		number of bytes to be copy
 *
 * @return		None
 */
extern void  bcopy(const void *src, void *dest, unsigned int count);

/*!
 * @brief copy memory area
 *
 * @param src		pointer to the destination array where the content is to be copied, type-casted to a pointer of type void*
 * @param dest		pointer to the source of data to be copied, type-casted to a pointer of type void*
 * @param count		number of bytes to be copied
 *
 * @return		None
 */
extern void * memcpy(void *, const void *, unsigned long);
extern void * memcpy32(void *dest, const void *src, unsigned long count);

/*!
 * @brief copy blocks of data by load and store multiple register
 *
 * @param src		pointer to the destination array where the content is to be copied, type-casted to a pointer of type void*
 * @param dest		pointer to the source of data to be copied, type-casted to a pointer of type void*
 * @param count		number of bytes to be copied, which must be 32 bytes aligned
 *
 * @return		None
 */
void fastcpy(void *dest, const void *src, unsigned long count);

/*!
 * @brief copy memory area
 *
 * @param dest		pointer to the destination array where the content is to be copied, type-casted to a pointer of type void*
 * @param src		pointer to the source of data to be copied, type-casted to a pointer of type void*
 * @param count		number of bytes to be copied
 *
 * @return		None
 */
extern void * memmove(void *, const void *, unsigned long);

/*!
 * @brief find the first occurrence of byte 'c', or 1 past the area if none
 *
 * @param addr	the memory area to be scanned
 * @param c	the byte to search for
 * @param size	maximum number of bytes to be scanned
 *
 * @return	None
 */
extern void * memscan(void *, int, unsigned long);

/*!
 * @brief compare memory ares
 *
 * @param cs		pointer to a block of memory
 * @param ct		pointer to a block of memory
 * @param count		number of bytes to be compared
 *
 * @return		Return value < 0 if @cs was less than @ct.
 * 			Return value > 0 if @ct was less than @cs.
 * 			Return value = 0 if @cs was equal to @ct.
 */
extern int memcmp(const void *, const void *, unsigned long);
/*! @} */
#endif /* _STRING_H_ */
