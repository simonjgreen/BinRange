"""Focused tests for the guarded, local WebServer multipart parser replacement."""

import hashlib
import os
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

import anchor_web_build as web


ROOT = pathlib.Path(__file__).resolve().parents[1]
PLATFORMIO_PACKAGES = pathlib.Path(
    os.environ.get("BINRANGE_PLATFORMIO_PACKAGES",
                   pathlib.Path.home() / ".platformio" / "packages")
)
FRAMEWORK = PLATFORMIO_PACKAGES / "framework-arduinoespressif32@3.20014.231204"


def verified_source():
    source = FRAMEWORK / web.SOURCE_NAME
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != web.PRISTINE_SHA256:
        raise RuntimeError("missing verified Arduino 2.0.14 WebServer Parsing.cpp fixture")
    return source


def extract_helper(source, signature):
    start = source.index(signature)
    depth = 0
    for pos in range(start, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError("unterminated helper")


def extract_function(source, signature):
    return extract_helper(source, signature)


class FakeEnvironment:
    def __init__(self):
        self.middleware = []

    def AddBuildMiddleware(self, callback, pattern):
        self.middleware.append((callback, pattern))

    def File(self, path):
        return pathlib.Path(path)


class AnchorWebBuildTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tempdir.name)

    def tearDown(self):
        self.tempdir.cleanup()

    def test_verified_framework_source_is_copied_and_patched_locally(self):
        framework = self.root / "framework"
        source = framework / web.SOURCE_NAME
        source.parent.mkdir(parents=True)
        shutil.copy2(verified_source(), source)
        (framework / "package.json").write_text(
            '{"name":"framework-arduinoespressif32","version":"3.20014.231204"}',
            encoding="utf-8",
        )

        replacement = web.prepare_replacement(framework, self.root / "build")

        self.assertNotEqual(replacement, source)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), web.PRISTINE_SHA256)
        self.assertEqual(hashlib.sha256(replacement.read_bytes()).hexdigest(), web.PATCHED_SHA256)
        patched = replacement.read_text(encoding="utf-8")
        self.assertNotIn("readStringUntil", patched)
        self.assertIn("ANCHOR_MAX_TEXT_FIELD = 1024", patched)
        self.assertIn("ANCHOR_MAX_HEADERS = 4096", patched)
        self.assertIn("ANCHOR_MAX_BODY_WAIT = 60000", patched)
        self.assertIn("_clientContentLength > ANCHOR_MAX_TEXT_FIELD", patched)
        self.assertIn("(_clientContentLength && !plainBuf) || plainLength < _clientContentLength", patched)

    def test_modified_framework_source_is_rejected_without_local_replacement(self):
        framework = self.root / "framework"
        source = framework / web.SOURCE_NAME
        source.parent.mkdir(parents=True)
        source.write_bytes(verified_source().read_bytes() + b"\n// unexpected change\n")
        (framework / "package.json").write_text(
            '{"name":"framework-arduinoespressif32","version":"3.20014.231204"}',
            encoding="utf-8",
        )

        with self.assertRaises(web.PatchError):
            web.prepare_replacement(framework, self.root / "build")
        self.assertFalse((self.root / "build" / "anchor-webserver" / "Parsing.cpp").exists())

    def test_middleware_replaces_only_webserver_parsing_source(self):
        replacement = self.root / "build" / "anchor-webserver" / "Parsing.cpp"
        replacement.parent.mkdir(parents=True)
        replacement.write_text("patched", encoding="utf-8")
        env = FakeEnvironment()

        web.install_middleware(env, replacement)

        self.assertEqual(len(env.middleware), 1)
        callback, pattern = env.middleware[0]
        self.assertEqual(pattern, "*/libraries/WebServer/src/Parsing.cpp")
        self.assertEqual(callback(env, object()), replacement)

    def test_patched_parser_bounds_accumulators_deadline_and_real_multipart_streams(self):
        source = web.patched_source(verified_source().read_text(encoding="utf-8"))
        expired = extract_helper(source, "static bool anchorExpired(")
        read_byte = extract_helper(source, "static bool anchorReadByte(")
        read_line = extract_helper(source, "static bool anchorReadLine(")
        read_header = extract_helper(source, "static bool anchorReadHeaderLine(")
        read_bytes = extract_function(source, "static char* readBytesWithTimeout(")
        parse_form = extract_function(source, "bool WebServer::_parseForm(")
        harness = self.root / "multipart_parser.cpp"
        harness.write_text(textwrap.dedent(f"""
            #include <assert.h>
            #include <algorithm>
            #include <cstdlib>
            #include <cstring>
            #include <stddef.h>
            #include <stdint.h>
            #include <memory>
            #include <string>
            #include <vector>
            typedef unsigned long ulong;
            static ulong now = 0;
            ulong millis() {{ return now; }}
            void delay(unsigned long ms) {{ now += ms; }}
            #define HTTP_MAX_DATA_WAIT 5000
            #define WEBSERVER_MAX_POST_ARGS 32
            #define HTTP_UPLOAD_BUFLEN 1436
            #define F(x) x
            #define FPSTR(x) x
            static const size_t ANCHOR_MAX_HEADER_LINE = 1024;
            static const size_t ANCHOR_MAX_HEADERS = 4096;
            static const size_t ANCHOR_MAX_TEXT_FIELD = 1024;
            static const uint32_t ANCHOR_MAX_BODY_WAIT = 60000;
            class String {{
             public:
              std::string value;
              String() = default;
              String(const char *text) : value(text) {{}}
              String &operator=(const char *text) {{ value = text; return *this; }}
              String &operator+=(char c) {{ value += c; return *this; }}
              String &operator+=(const char *text) {{ value += text; return *this; }}
              String &operator+=(const String &text) {{ value += text.value; return *this; }}
              size_t length() const {{ return value.length(); }}
              const char *c_str() const {{ return value.c_str(); }}
              bool operator==(const char *text) const {{ return value == text; }}
              bool operator!=(const char *text) const {{ return value != text; }}
              bool operator==(const String &other) const {{ return value == other.value; }}
              bool operator!=(const String &other) const {{ return value != other.value; }}
              bool startsWith(const String &prefix) const {{ return value.rfind(prefix.value, 0) == 0; }}
              bool equalsIgnoreCase(const char *other) const {{
                if (value.size() != std::char_traits<char>::length(other)) return false;
                for (size_t i = 0; i < value.size(); ++i)
                  if ((value[i] | 32) != (other[i] | 32)) return false;
                return true;
              }}
              int indexOf(char c) const {{ auto p = value.find(c); return p == std::string::npos ? -1 : int(p); }}
              String substring(size_t start) const {{ return start > value.size() ? String() : String(value.substr(start).c_str()); }}
              String substring(size_t start, size_t end) const {{
                return start > value.size() ? String() : String(value.substr(start, end - start).c_str());
              }}
            }};
            String operator+(const char *left, const String &right) {{ return String((std::string(left) + right.value).c_str()); }}
            String operator+(const String &left, const char *right) {{ return String((left.value + right).c_str()); }}
            class WiFiClient {{
             public:
              std::vector<int> bytes; size_t next = 0; bool live = true; bool continuous = false;
              int read() {{ return next < bytes.size() ? unsigned(uint8_t(bytes[next++])) : -1; }}
              bool connected() const {{ return live; }}
              size_t available() const {{ return continuous ? 1 : bytes.size() - next; }}
              size_t readBytes(char *out, size_t count) {{
                if (continuous) {{ out[0] = 'x'; now += 60000; return 1; }}
                size_t copied = std::min(count, bytes.size() - next);
                for (size_t i = 0; i < copied; ++i) out[i] = char(bytes[next++]);
                return copied;
              }}
            }};
            struct RequestArgument {{ String key, value; }};
            enum UploadStatus {{ UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED }};
            struct HTTPUpload {{
              UploadStatus status; String name, filename, type; size_t totalSize = 0, currentSize = 0;
              uint8_t buf[HTTP_UPLOAD_BUFLEN] = {{}};
            }};
            class WebServer;
            class RequestHandler {{
             public:
              virtual bool canUpload(const String &) {{ return true; }}
              virtual void upload(WebServer &, const String &, HTTPUpload &) {{}}
              virtual ~RequestHandler() = default;
            }};
            namespace mime {{ enum {{ txt }}; struct Mime {{ const char *mimeType; }}; static Mime mimeTable[] = {{{{"text/plain"}}}}; }}
            static const char Content_Type[] = "Content-Type";
            static const char filename[] = "filename";
            class WebServer {{
             public:
              RequestArgument *_postArgs = nullptr, *_currentArgs = new RequestArgument[1];
              int _postArgsLen = 0, _currentArgCount = 0; std::unique_ptr<HTTPUpload> _currentUpload;
              RequestHandler *_currentHandler = nullptr; String _currentUri = "/release"; bool aborted = false;
              ~WebServer() {{ delete[] _postArgs; delete[] _currentArgs; }}
              bool hasArg(const char *) const {{ return false; }}
              String arg(const char *) const {{ return String(); }}
              int _uploadReadByte(WiFiClient &client) {{ return client.read(); }}
              void _uploadWriteByte(uint8_t byte) {{
                if (_currentUpload->currentSize == HTTP_UPLOAD_BUFLEN) {{
                  if (_currentHandler && _currentHandler->canUpload(_currentUri))
                    _currentHandler->upload(*this, _currentUri, *_currentUpload);
                  _currentUpload->totalSize += _currentUpload->currentSize;
                  _currentUpload->currentSize = 0;
                }}
                _currentUpload->buf[_currentUpload->currentSize++] = byte;
              }}
              bool _parseFormUploadAborted() {{ aborted = true; return false; }}
              bool _parseForm(WiFiClient &, String, uint32_t);
            }};
            class CaptureHandler : public RequestHandler {{
             public:
              std::vector<uint8_t> payload; bool ended = false;
              void upload(WebServer &, const String &, HTTPUpload &up) override {{
                if (up.status == UPLOAD_FILE_WRITE)
                  payload.insert(payload.end(), up.buf, up.buf + up.currentSize);
                if (up.status == UPLOAD_FILE_END) ended = true;
              }}
            }};
            {expired}
            {read_byte}
            {read_line}
            {read_header}
            {read_bytes}
            {parse_form}
            int main() {{
              String line;
              WiFiClient normal{{{{'m','a','n','i','f','e','s','t','\\r','\\n'}}}};
              assert(anchorReadLine(normal, line, 1024, 0, 60000) && line == "manifest");
              WiFiClient disconnected{{{{'x'}}}}; disconnected.live = false; now = 0;
              assert(!anchorReadLine(disconnected, line, 1024, 0, 60000) && now == 0);
              WiFiClient truncated; now = 0;
              assert(!anchorReadLine(truncated, line, 1024, 0, 60000) && now >= 60000);
              WiFiClient header{{{{'\\r','\\n'}}}}; size_t total = 4096; now = 0;
              assert(!anchorReadHeaderLine(header, line, total, 0));
              WiFiClient continuous; continuous.continuous = true; size_t received = 0; now = 0;
              char *plain = readBytesWithTimeout(continuous, 2, received, 60000);
              assert(received == 1); free(plain);
              const std::string boundary = "BOUNDARY";
              const std::string prefix = "--" + boundary + "\\r\\nContent-Disposition: form-data; name=\\\"manifest\\\"\\r\\n\\r\\n";
              WiFiClient cut; cut.bytes.assign(prefix.begin(), prefix.end());
              WebServer cut_server; CaptureHandler cut_handler; cut_server._currentHandler = &cut_handler; now = 0;
              assert(!cut_server._parseForm(cut, String(boundary.c_str()), uint32_t(cut.bytes.size())));
              assert(cut_handler.payload.empty() && !cut_handler.ended);
              std::string oversized = prefix + std::string(1024, 'a') + "\\r\\nx\\r\\n--" + boundary + "--\\r\\n";
              WiFiClient oversized_text; oversized_text.bytes.assign(oversized.begin(), oversized.end());
              WebServer text_server; CaptureHandler text_handler; text_server._currentHandler = &text_handler; now = 0;
              assert(!text_server._parseForm(oversized_text, String(boundary.c_str()), uint32_t(oversized.size())));
              std::vector<uint8_t> image(203 * 1024, 0xa5);
              std::string request = prefix + "{{\\\"version\\\":\\\"1\\\"}}\\r\\n--" + boundary +
                  "\\r\\nContent-Disposition: form-data; name=\\\"file\\\"; filename=\\\"release.bin\\\"\\r\\n" +
                  "Content-Type: application/octet-stream\\r\\n\\r\\n";
              request.append(reinterpret_cast<const char *>(image.data()), image.size());
              request += "\\r\\n--" + boundary + "--\\r\\n";
              WiFiClient upload; upload.bytes.assign(request.begin(), request.end());
              WebServer upload_server; CaptureHandler upload_handler; upload_server._currentHandler = &upload_handler; now = 0;
              assert(upload_server._parseForm(upload, String(boundary.c_str()), uint32_t(request.size())));
              assert(upload_handler.ended && upload_handler.payload == image);
              return 0;
            }}
        """), encoding="utf-8")
        binary = self.root / "multipart_parser"
        subprocess.run(["c++", "-std=c++17", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
