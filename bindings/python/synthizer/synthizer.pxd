cdef extern from "synthizer.h":

    ctypedef unsigned int syz_Handle

    ctypedef int syz_ErrorCode

    cdef enum SYZ_LOGGING_BACKEND:
        SYZ_LOGGING_BACKEND_STDERR

    syz_ErrorCode syz_configureLoggingBackend(SYZ_LOGGING_BACKEND backend, void* param)

    syz_ErrorCode syz_getLastErrorCode()

    char* syz_getLastErrorMessage()

    syz_ErrorCode syz_handleIncRef(syz_Handle handle)

    syz_ErrorCode syz_handleDecRef(syz_Handle handle)

    syz_ErrorCode syz_initialize()

    syz_ErrorCode syz_shutdown()

    syz_ErrorCode syz_getI(int* out, syz_Handle target, int property)

    syz_ErrorCode syz_setI(syz_Handle target, int property, int value)

    syz_ErrorCode syz_getD(double* out, syz_Handle target, int property)

    syz_ErrorCode syz_setD(syz_Handle target, int property, double value)

    syz_ErrorCode syz_getO(syz_Handle* out, syz_Handle target, int property)

    syz_ErrorCode syz_setO(syz_Handle target, int property, syz_Handle value)

    syz_ErrorCode syz_createContext(syz_Handle* out)

    syz_ErrorCode syz_createStreamingGenerator(syz_Handle* out, syz_Handle context, char* protocol, char* path, char* options)

    syz_ErrorCode syz_sourceAddGenerator(syz_Handle source, syz_Handle generator)

    syz_ErrorCode syz_sourceRemoveGenerator(syz_Handle source, syz_Handle generator)

    syz_ErrorCode syz_createPannedSource(syz_Handle* out, syz_Handle context)