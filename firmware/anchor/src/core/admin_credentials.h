#pragma once

#include <cstddef>

enum class AdminCredentialRead { Found, NotFound, Error };

class AdminCredentialIo {
  public:
    virtual ~AdminCredentialIo() = default;
    virtual AdminCredentialRead read(char *out, size_t *length) = 0;
    virtual bool write(const char *value, size_t length) = 0;
    virtual bool commit() = 0;
};

class AdminCredentialFailureIo {
  public:
    virtual ~AdminCredentialFailureIo() = default;
    virtual void disable_admin_and_ota() = 0;
};

// Password bytes remain owned by the runtime caller. This policy only
// validates and atomically sequences the low-level NVS operations.
bool admin_load_credential(AdminCredentialIo &io, const char *bootstrap,
                           size_t bootstrap_length, char out[65], size_t *length);
bool admin_store_credential(AdminCredentialIo &io, const char *value, size_t length);
bool admin_load_credential_or_disable(AdminCredentialIo &io, const char *bootstrap,
                                      size_t bootstrap_length, char out[65], size_t *length,
                                      AdminCredentialFailureIo &failure);
bool admin_store_credential_or_disable(AdminCredentialIo &io, const char *value,
                                       size_t length, AdminCredentialFailureIo &failure);
