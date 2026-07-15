/* PSEH2 stub for MinNT - compile-only stubs, no runtime SEH */
#ifndef _PSEH2_H_
#define _PSEH2_H_

/* These are the simplest possible stubs: just make the code compile.
 * SEH exception handling is NOT functional at runtime.
 */
extern unsigned long __seh_code;

#define _SEH2_TRY \
    do { __seh_code = 0; if(1) {
#define _SEH2_EXCEPT(filter_expr) \
    __seh_code = 0; } if(0 && (filter_expr)) {
#define _SEH2_FINALLY \
    __seh_code = 0; } if(1) {
#define _SEH2_END \
    __seh_code = 0; } } while(0)

#define _SEH2_LEAVE        break
#define _SEH2_GetExceptionCode()    (__seh_code)
#define _SEH2_AbnormalTermination() 0
#define _SEH2_FILTER(x)  ((x) != 0)
#define _SEH2_GetExceptionInformation() ((void*)0)

/* Also support without underscore prefix */
#define SEH2_TRY           _SEH2_TRY
#define SEH2_EXCEPT(x)     _SEH2_EXCEPT(x)
#define SEH2_FINALLY       _SEH2_FINALLY
#define SEH2_END           _SEH2_END
#define SEH2_LEAVE         _SEH2_LEAVE
#define SEH2_GetExceptionCode()    _SEH2_GetExceptionCode()
#define SEH2_AbnormalTermination() _SEH2_AbnormalTermination()
#define SEH2_FILTER(x)     _SEH2_FILTER(x)
#define SEH2_GetExceptionInformation() _SEH2_GetExceptionInformation()

#define TRY         SEH2_TRY
#define EXCEPT(x)   SEH2_EXCEPT(x)
#define FINALLY     SEH2_FINALLY
#define ENDTRY      SEH2_END
#define GetExceptionCode()      SEH2_GetExceptionCode()
#define AbnormalTermination()   SEH2_AbnormalTermination()

#endif /* _PSEH2_H_ */
