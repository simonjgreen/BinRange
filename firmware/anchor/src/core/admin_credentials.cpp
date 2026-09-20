#include "core/admin_credentials.h"

#include "core/admin_policy.h"

#include <cstring>

bool admin_store_credential(AdminCredentialIo &io, const char *value, size_t length) {
    return admin_password_valid(value, length) && io.write(value, length + 1) &&
           io.commit();
}

bool admin_load_credential(AdminCredentialIo &io, const char *bootstrap,
                           size_t bootstrap_length, char out[65], size_t *length) {
    if (!out || !length || *length < 65) return false;
    size_t stored_length = 65;
    const AdminCredentialRead result = io.read(out, &stored_length);
    if (result == AdminCredentialRead::Found) {
        if (stored_length < 13 || stored_length > 65 || out[stored_length - 1] != '\0' ||
            !admin_password_valid(out, stored_length - 1))
            return false;
        *length = stored_length - 1;
        return true;
    }
    if (result != AdminCredentialRead::NotFound ||
        !admin_password_valid(bootstrap, bootstrap_length) ||
        !admin_store_credential(io, bootstrap, bootstrap_length))
        return false;
    memcpy(out, bootstrap, bootstrap_length);
    out[bootstrap_length] = '\0';
    *length = bootstrap_length;
    return true;
}

bool admin_load_credential_or_disable(AdminCredentialIo &io, const char *bootstrap,
                                      size_t bootstrap_length, char out[65], size_t *length,
                                      AdminCredentialFailureIo &failure) {
    if (admin_load_credential(io, bootstrap, bootstrap_length, out, length)) return true;
    failure.disable_admin_and_ota();
    return false;
}

bool admin_store_credential_or_disable(AdminCredentialIo &io, const char *value,
                                       size_t length, AdminCredentialFailureIo &failure) {
    if (admin_store_credential(io, value, length)) return true;
    failure.disable_admin_and_ota();
    return false;
}
