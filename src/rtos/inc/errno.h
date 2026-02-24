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

#ifndef _ERRNO_H_
#define _ERRNO_H_

enum {
    ENOERR			=  0,
    EDOM			= -1,
    ERANGE			= -2,
    ENOSYS			= -3,
    EINVAL			= -4,
    ESPIPE			= -5,
    EBADF			= -6,
    ENOMEM			= -7,
    EACCES			= -8,
    ENFILE			= -9,
    EMFILE			= -10,
    ENAMETOOLONG	= -11,
    ELOOP			= -12,
    ENOMSG			= -13,
    E2BIG			= -14,
    EINTR			= -15,
    EILSEQ			= -16,
    ENOEXEC			= -17,
    ENOENT			= -18,
    EPROTOTYPE		= -19,
    ESRCH			= -20,
    EPERM			= -21,
    ENOTDIR			= -22,
    ESTALE			= -23,
    EISDIR			= -24,
    EOPNOTSUPP		= -25,
    ENOTTY			= -26,
    EAGAIN			= -27,
    EIO				= -28,
    ENOSPC			= -29,
    EEXIST			= -30,
    EBUSY			= -31,
    EOVERFLOW		= -32,
};

#endif
