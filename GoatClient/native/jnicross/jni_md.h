/*
 * Minimal win32 jni_md.h for cross-compiling the native DLL with llvm-mingw
 * on any host. Matches the JDK's win32 definitions exactly; without it the
 * build depends on finding a Windows JDK for headers alone.
 */
#ifndef _JAVASOFT_JNI_MD_H_
#define _JAVASOFT_JNI_MD_H_

#define JNIEXPORT __declspec(dllexport)
#define JNIIMPORT __declspec(dllimport)
#define JNICALL __stdcall

typedef long jint;
#ifdef __GNUC__
typedef __int64 jlong;
#else
typedef _int64 jlong;
#endif
typedef signed char jbyte;

#endif /* _JAVASOFT_JNI_MD_H_ */
