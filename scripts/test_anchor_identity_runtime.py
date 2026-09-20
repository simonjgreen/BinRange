"""Actual identity/NVS adapter: persistence, wrong-board recovery and fail-closed I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cassert>
#include <cstring>
#include "nvs.h"
#include "update_identity.cpp"
const uint8_t board[] = {2,3,4,5,6,7};
const uint8_t first[] = {1,2,3,4,5,0xc6};
const uint8_t later[] = {7,8,9,10,11,0xcc};
const UpdateIdentityRecovery recovery{{2,3,4,5,6,7}, {12,13,14,15,16,0xd1}};
uint8_t out[6];
void reset() { identity_fake::saved.clear(); identity_fake::fail=0; identity_fake::writes=0; assert(!identity_fake::live); }
bool load(bool known, const UpdateIdentityRecovery *r=nullptr, const uint8_t *candidate=first) {
    memset(out, 0xff, 6);
    bool ok=update_identity_load(board,candidate,known,r,out);
    assert(!identity_fake::live);
    if (!ok) { const uint8_t zero[6]{}; assert(!memcmp(out,zero,6)); }
    return ok;
}
int main() {
    reset(); assert(load(false)); assert(!memcmp(out,first,6));
    assert(load(true,nullptr,later)); assert(!memcmp(out,first,6)); assert(identity_fake::writes==1);
    reset(); assert(!load(true)); assert(identity_fake::writes==0);
    reset(); assert(load(true,&recovery)); assert(!memcmp(out,recovery.address,6));
    assert(load(true,nullptr,later)); assert(!memcmp(out,recovery.address,6)); assert(identity_fake::writes==1);
    auto good=identity_fake::saved;
    auto wrong=recovery; wrong.board_mac[0]=4;
    reset(); assert(!load(true,&wrong)); assert(identity_fake::writes==0);
    reset(); wrong=recovery; wrong.address[5]=0x81; assert(!load(true,&wrong));
    for (int failure=1; failure<=6; ++failure) {
        reset(); identity_fake::fail=failure; assert(!load(true,&recovery));
    }
    for (int failure=1; failure<=3; ++failure) {
        reset(); identity_fake::saved=good; identity_fake::fail=failure;
        assert(!load(true,&recovery)); assert(identity_fake::writes==0);
    }
    reset(); identity_fake::saved=good; identity_fake::saved[0]^=1;
    assert(!load(true,&recovery)); assert(identity_fake::writes==0);
    reset(); identity_fake::saved=good; identity_fake::saved[4]^=1;
    assert(!load(true,&recovery)); assert(identity_fake::writes==0);
    reset(); identity_fake::saved=good; identity_fake::saved.pop_back();
    assert(!load(true,&recovery)); assert(identity_fake::writes==0);
    reset(); identity_fake::saved=good;
    wrong=recovery; wrong.address[0]^=1; assert(!load(true,&wrong)); assert(identity_fake::writes==0);
}
'''


class IdentityRuntimeTests(unittest.TestCase):
    def test_identity_is_durable_and_never_replaced_on_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'identity-test'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT/'firmware/anchor/test/test_update_identity/stubs'),
                            '-I', str(ROOT/'firmware/anchor/src'), '-x', 'c++', '-', '-o', str(exe)],
                           input=HARNESS, text=True, check=True, capture_output=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)


if __name__ == '__main__':
    unittest.main()
