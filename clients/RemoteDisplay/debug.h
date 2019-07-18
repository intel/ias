#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdio.h>

enum {
	DBG_OFF,
	DBG_INFO,
	DBG_DBG,
	DBG_VERBOSE,
	DBG_TRACE,
};

/*
 * Set it to 0 or higher with 0 being lowest number of messages
 * 0 = Only errors show up
 * 1 = Errors + Info messages
 * 2 = Errors + Info messages + Debug messages
 * 3 = Errors + Info messages + Debug messages + Trace calls
 */
#define DEBUG 1

extern int debug_level;

#define PRINT(fmt, ...)   fprintf(stdout, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)   { fprintf(stderr, "ERROR: "); fprintf(stderr, fmt, ##__VA_ARGS__); }
#define WARN(fmt, ...)    { fprintf(stderr, "WARNING: "); fprintf(stderr, fmt, ##__VA_ARGS__); }

#if DEBUG
#define INFO(fmt, ...)    if(debug_level >= DBG_INFO)    PRINT(fmt, ##__VA_ARGS__)
#define DBG(fmt, ...)     if(debug_level >= DBG_DBG)     PRINT(fmt, ##__VA_ARGS__)
#define VERBOSE(fmt, ...) if(debug_level >= DBG_VERBOSE) PRINT(fmt, ##__VA_ARGS__)
#define TRACE(fmt, ...)   if(debug_level >= DBG_TRACE)   PRINT(fmt, ##__VA_ARGS__)

#ifdef __cplusplus
#define TRACING()  tracer trace(__FUNCTION__);
#else
#define TRACING()
#endif

#else
#define INFO(fmt, ...)
#define DBG(fmt, ...)
#define VERBOSE(fmt, ...)
#define TRACE(fmt, ...)
#define TRACING()
#endif

#ifdef __cplusplus
class tracer {
private:
	char *m_func_name;
public:
	tracer(const char *func_name)
	{
		TRACE("Entering %s\n", func_name);
		m_func_name = (char *) func_name;
	}
	~tracer() { TRACE("Exiting %s\n", m_func_name); }
};
#endif


#endif

