"""Build a guarded, project-local replacement for Arduino 2.0.14 WebServer parsing."""

import hashlib
import json
import os
import pathlib
import tempfile


FRAMEWORK_NAME = "framework-arduinoespressif32"
FRAMEWORK_VERSION = "3.20014.231204"
SOURCE_NAME = "libraries/WebServer/src/Parsing.cpp"
PRISTINE_SHA256 = "6ffc23d69143515d65d7fcf89528ebd5b9767a7d710d4bd6e652c95ba8ddd26b"
PATCHED_SHA256 = "401b73083fe0997f3a7b87f2a64691af6d7e1f8574324e090ee24eff9b7d78d6"
MIDDLEWARE_PATTERN = "*/libraries/WebServer/src/Parsing.cpp"


class PatchError(RuntimeError):
    pass


def _replace_function(source, signature, following_signature, replacement):
    start = source.find(signature)
    end = source.find(following_signature, start)
    if start == -1 or end == -1 or source.find(signature, start + 1) != -1:
        raise PatchError(f"unable to locate unique {signature} patch span")
    return source[:start] + replacement + "\n\n" + source[end:]


def _helpers():
    return r'''// Anchor policy: parser work is bounded before HTTP callbacks can authorize it.
static const size_t ANCHOR_MAX_REQUEST_LINE = 1024;
static const size_t ANCHOR_MAX_HEADER_LINE = 1024;
static const size_t ANCHOR_MAX_HEADERS = 4096;
static const size_t ANCHOR_MAX_TEXT_FIELD = 1024;
static const uint32_t ANCHOR_MAX_BODY_WAIT = 60000;

static bool anchorExpired(uint32_t started, uint32_t timeout) {
  return (uint32_t)(millis() - started) >= timeout;
}

static bool anchorReadByte(WiFiClient& client, uint8_t& byte, uint32_t started, uint32_t timeout) {
  while (!anchorExpired(started, timeout)) {
    if (!client.connected()) return false;
    const int value = client.read();
    if (value >= 0) {
      byte = (uint8_t)value;
      return true;
    }
    delay(1);
  }
  return false;
}

static bool anchorReadLine(WiFiClient& client, String& line, size_t maximum,
                           uint32_t started, uint32_t timeout) {
  line = "";
  for (;;) {
    uint8_t byte;
    if (!anchorReadByte(client, byte, started, timeout)) return false;
    if (byte == '\r') {
      if (!anchorReadByte(client, byte, started, timeout) || byte != '\n') return false;
      return true;
    }
    if (byte == '\n' || line.length() >= maximum) return false;
    line += (char)byte;
  }
}

static bool anchorReadHeaderLine(WiFiClient& client, String& line, size_t& total,
                                 uint32_t started) {
  if (!anchorReadLine(client, line, ANCHOR_MAX_HEADER_LINE, started, HTTP_MAX_DATA_WAIT)) return false;
  if (total > ANCHOR_MAX_HEADERS - 2) return false;
  if (line.length() > ANCHOR_MAX_HEADERS - total - 2) return false;
  total += line.length() + 2;
  return true;
}'''


def _parse_request():
    return r'''bool WebServer::_parseRequest(WiFiClient& client) {
  const uint32_t headersStarted = millis();
  size_t headerBytes = 0;
  String req;
  if (!anchorReadLine(client, req, ANCHOR_MAX_REQUEST_LINE, headersStarted, HTTP_MAX_DATA_WAIT)) return false;
  for (int i = 0; i < _headerKeysCount; ++i) _currentHeaders[i].value = String();

  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
    log_e("Invalid request: %s", req.c_str());
    return false;
  }

  String methodStr = req.substring(0, addr_start);
  String url = req.substring(addr_start + 1, addr_end);
  String versionEnd = req.substring(addr_end + 8);
  _currentVersion = atoi(versionEnd.c_str());
  String searchStr = "";
  int hasSearch = url.indexOf('?');
  if (hasSearch != -1) {
    searchStr = url.substring(hasSearch + 1);
    url = url.substring(0, hasSearch);
  }
  _currentUri = url;
  _chunked = false;
  _clientContentLength = 0;

  HTTPMethod method = HTTP_ANY;
  size_t num_methods = sizeof(_http_method_str) / sizeof(const char *);
  for (size_t i = 0; i < num_methods; i++) {
    if (methodStr == _http_method_str[i]) {
      method = (HTTPMethod)i;
      break;
    }
  }
  if (method == HTTP_ANY) {
    log_e("Unknown HTTP Method: %s", methodStr.c_str());
    return false;
  }
  _currentMethod = method;

  RequestHandler* handler;
  for (handler = _firstHandler; handler; handler = handler->next()) {
    if (handler->canHandle(_currentMethod, _currentUri)) break;
  }
  _currentHandler = handler;

  String boundaryStr;
  String headerName;
  String headerValue;
  bool isForm = false;
  bool isEncoded = false;
  while (true) {
    if (!anchorReadHeaderLine(client, req, headerBytes, headersStarted)) return false;
    if (req == "") break;
    const int headerDiv = req.indexOf(':');
    if (headerDiv == -1) return false;
    headerName = req.substring(0, headerDiv);
    headerValue = req.substring(headerDiv + 1);
    headerValue.trim();
    _collectHeader(headerName.c_str(), headerValue.c_str());
    if (headerName.equalsIgnoreCase(FPSTR(Content_Type))) {
      using namespace mime;
      if (headerValue.startsWith(FPSTR(mimeTable[txt].mimeType))) {
        isForm = false;
      } else if (headerValue.startsWith(F("application/x-www-form-urlencoded"))) {
        isForm = false;
        isEncoded = true;
      } else if (headerValue.startsWith(F("multipart/"))) {
        const int boundary = headerValue.indexOf('=');
        if (boundary == -1) return false;
        boundaryStr = headerValue.substring(boundary + 1);
        boundaryStr.replace("\"", "");
        if (!boundaryStr.length() || boundaryStr.length() > 70) return false;
        isForm = true;
      }
    } else if (headerName.equalsIgnoreCase(F("Content-Length"))) {
      _clientContentLength = headerValue.toInt();
      if (_clientContentLength == 0 && headerValue != "0") return false;
    } else if (headerName.equalsIgnoreCase(F("Host"))) {
      _hostHeader = headerValue;
    }
  }

  if (method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE) {
    if (!isForm) {
      if (_clientContentLength > ANCHOR_MAX_TEXT_FIELD) return false;
      size_t plainLength;
      char* plainBuf = readBytesWithTimeout(client, _clientContentLength, plainLength, ANCHOR_MAX_BODY_WAIT);
      if ((_clientContentLength && !plainBuf) || plainLength < _clientContentLength) {
        free(plainBuf);
        return false;
      }
      if (_clientContentLength > 0) {
        if (isEncoded) {
          if (searchStr != "") searchStr += '&';
          searchStr += plainBuf;
        }
        _parseArguments(searchStr);
        if (!isEncoded) {
          RequestArgument& arg = _currentArgs[_currentArgCount++];
          arg.key = F("plain");
          arg.value = String(plainBuf);
        }
        free(plainBuf);
      } else {
        _parseArguments(searchStr);
      }
    } else {
      _parseArguments(searchStr);
      if (!_parseForm(client, boundaryStr, _clientContentLength)) return false;
    }
  } else {
    _parseArguments(searchStr);
  }
  client.flush();
  return true;
}'''


def _read_bytes_with_timeout():
    return r'''static char* readBytesWithTimeout(WiFiClient& client, size_t maxLength, size_t& dataLength, int timeout_ms) {
  char *buf = nullptr;
  dataLength = 0;
  const uint32_t started = millis();
  while (dataLength < maxLength) {
    if (anchorExpired(started, timeout_ms)) return buf;
    size_t newLength = 0;
    while (!(newLength = client.available())) {
      if (!client.connected() || anchorExpired(started, timeout_ms)) return buf;
      delay(1);
    }
    if (newLength > maxLength - dataLength) newLength = maxLength - dataLength;
    char *newBuf = (char *)realloc(buf, dataLength + newLength + 1);
    if (!newBuf) {
      free(buf);
      dataLength = 0;
      return nullptr;
    }
    buf = newBuf;
    const size_t read = client.readBytes(buf + dataLength, newLength);
    dataLength += read;
    buf[dataLength] = '\0';
    if (read != newLength && anchorExpired(started, timeout_ms)) return buf;
  }
  return buf;
}'''


def _parse_form():
    return r'''bool WebServer::_parseForm(WiFiClient& client, String boundary, uint32_t len) {
  if (!len || !boundary.length() || boundary.length() > 70) return false;
  const uint32_t bodyStarted = millis();
  size_t partHeaderBytes = 0;
  String line;
  auto readLine = [&](String& out, size_t maximum) {
    return anchorReadLine(client, out, maximum, bodyStarted, ANCHOR_MAX_BODY_WAIT);
  };
  auto readPartHeader = [&](String& out) {
    if (!readLine(out, ANCHOR_MAX_HEADER_LINE)) return false;
    if (partHeaderBytes > ANCHOR_MAX_HEADERS - 2) return false;
    if (out.length() > ANCHOR_MAX_HEADERS - partHeaderBytes - 2) return false;
    partHeaderBytes += out.length() + 2;
    return true;
  };
  auto readUploadByte = [&]() -> int {
    if (anchorExpired(bodyStarted, ANCHOR_MAX_BODY_WAIT)) return -1;
    const int byte = _uploadReadByte(client);
    return anchorExpired(bodyStarted, ANCHOR_MAX_BODY_WAIT) ? -1 : byte;
  };

  if (!readLine(line, ANCHOR_MAX_HEADER_LINE) || line != ("--" + boundary)) return false;
  if (_postArgs) delete[] _postArgs;
  _postArgs = new RequestArgument[WEBSERVER_MAX_POST_ARGS];
  _postArgsLen = 0;
  while (true) {
    String argName;
    String argValue;
    String argType;
    String argFilename;
    bool argIsFile = false;

    if (!readPartHeader(line) || line.length() <= 19 ||
        !line.substring(0, 19).equalsIgnoreCase(F("Content-Disposition"))) return false;
    int nameStart = line.indexOf('=');
    if (nameStart == -1) return false;
    argName = line.substring(nameStart + 2);
    nameStart = argName.indexOf('=');
    if (nameStart == -1) {
      if (!argName.length()) return false;
      argName = argName.substring(0, argName.length() - 1);
    } else {
      argFilename = argName.substring(nameStart + 2, argName.length() - 1);
      argName = argName.substring(0, argName.indexOf('"'));
      argIsFile = true;
      if (argFilename == F("blob") && hasArg(FPSTR(filename))) argFilename = arg(FPSTR(filename));
    }
    if (!argName.length() || argName.length() > 64) return false;

    using namespace mime;
    argType = FPSTR(mimeTable[txt].mimeType);
    if (!readPartHeader(line)) return false;
    if (line.length() > 12 && line.substring(0, 12).equalsIgnoreCase(FPSTR(Content_Type))) {
      argType = line.substring(line.indexOf(':') + 2);
      if (!readPartHeader(line) || line.length()) return false;
    } else if (line.length()) {
      return false;
    }

    if (!argIsFile) {
      while (true) {
        if (!readLine(line, ANCHOR_MAX_TEXT_FIELD)) return false;
        if (line.startsWith("--" + boundary)) break;
        const size_t separator = argValue.length() ? 1 : 0;
        if (argValue.length() > ANCHOR_MAX_TEXT_FIELD - separator ||
            line.length() > ANCHOR_MAX_TEXT_FIELD - separator - argValue.length()) return false;
        if (separator) argValue += "\n";
        argValue += line;
      }
      if (_postArgsLen >= WEBSERVER_MAX_POST_ARGS) return false;
      RequestArgument& arg = _postArgs[_postArgsLen++];
      arg.key = argName;
      arg.value = argValue;
      if (line == ("--" + boundary + "--")) break;
      if (line != ("--" + boundary)) return false;
      continue;
    }

    _currentUpload.reset(new HTTPUpload());
    _currentUpload->status = UPLOAD_FILE_START;
    _currentUpload->name = argName;
    _currentUpload->filename = argFilename;
    _currentUpload->type = argType;
    _currentUpload->totalSize = 0;
    _currentUpload->currentSize = 0;
    if (_currentHandler && _currentHandler->canUpload(_currentUri))
      _currentHandler->upload(*this, _currentUri, *_currentUpload);
    _currentUpload->status = UPLOAD_FILE_WRITE;
    int argByte = readUploadByte();
readfile:
    while (argByte != 0x0D) {
      if (argByte < 0) return _parseFormUploadAborted();
      _uploadWriteByte(argByte);
      argByte = readUploadByte();
    }
    argByte = readUploadByte();
    if (argByte < 0) return _parseFormUploadAborted();
    if (argByte == 0x0A) {
      argByte = readUploadByte();
      if (argByte < 0) return _parseFormUploadAborted();
      if ((char)argByte != '-') {
        _uploadWriteByte(0x0D); _uploadWriteByte(0x0A); goto readfile;
      }
      argByte = readUploadByte();
      if (argByte < 0) return _parseFormUploadAborted();
      if ((char)argByte != '-') {
        _uploadWriteByte(0x0D); _uploadWriteByte(0x0A); _uploadWriteByte('-'); goto readfile;
      }
      uint8_t endBuf[70];
      for (uint32_t i = 0; i < boundary.length(); ++i) {
        argByte = readUploadByte();
        if (argByte < 0) return _parseFormUploadAborted();
        if ((char)argByte == 0x0D) {
          _uploadWriteByte(0x0D); _uploadWriteByte(0x0A); _uploadWriteByte('-'); _uploadWriteByte('-');
          for (uint32_t j = 0; j < i; ++j) _uploadWriteByte(endBuf[j]);
          goto readfile;
        }
        endBuf[i] = (uint8_t)argByte;
      }
      if (memcmp(endBuf, boundary.c_str(), boundary.length()) != 0) {
        _uploadWriteByte(0x0D); _uploadWriteByte(0x0A); _uploadWriteByte('-'); _uploadWriteByte('-');
        for (uint32_t i = 0; i < boundary.length(); ++i) _uploadWriteByte(endBuf[i]);
        argByte = readUploadByte();
        goto readfile;
      }
      if (_currentHandler && _currentHandler->canUpload(_currentUri))
        _currentHandler->upload(*this, _currentUri, *_currentUpload);
      _currentUpload->totalSize += _currentUpload->currentSize;
      _currentUpload->status = UPLOAD_FILE_END;
      if (_currentHandler && _currentHandler->canUpload(_currentUri))
        _currentHandler->upload(*this, _currentUri, *_currentUpload);
      if (!readLine(line, 2)) return _parseFormUploadAborted();
      if (line == "--") break;
      if (line != "") return _parseFormUploadAborted();
      continue;
    }
    _uploadWriteByte(0x0D);
    goto readfile;
  }

  int totalArgs = ((WEBSERVER_MAX_POST_ARGS - _postArgsLen) < _currentArgCount)
      ? (WEBSERVER_MAX_POST_ARGS - _postArgsLen) : _currentArgCount;
  for (int iarg = 0; iarg < totalArgs; ++iarg) {
    RequestArgument& arg = _postArgs[_postArgsLen++];
    arg.key = _currentArgs[iarg].key;
    arg.value = _currentArgs[iarg].value;
  }
  if (_currentArgs) delete[] _currentArgs;
  _currentArgs = new RequestArgument[_postArgsLen];
  for (int iarg = 0; iarg < _postArgsLen; ++iarg) {
    RequestArgument& arg = _currentArgs[iarg];
    arg.key = _postArgs[iarg].key;
    arg.value = _postArgs[iarg].value;
  }
  _currentArgCount = _postArgsLen;
  delete[] _postArgs;
  _postArgs = nullptr;
  _postArgsLen = 0;
  return true;
}'''


def patched_source(source):
    source = _replace_function(
        source,
        "static char* readBytesWithTimeout(WiFiClient& client, size_t maxLength, size_t& dataLength, int timeout_ms)",
        "bool WebServer::_parseRequest(",
        _read_bytes_with_timeout(),
    )
    source = _replace_function(
        source,
        "bool WebServer::_parseRequest(WiFiClient& client) {",
        "bool WebServer::_collectHeader(",
        _parse_request(),
    )
    source = _replace_function(
        source,
        "bool WebServer::_parseForm(WiFiClient& client, String boundary, uint32_t len){",
        "String WebServer::urlDecode(",
        _parse_form(),
    )
    include = '#include "detail/mimetable.h"\n'
    if source.count(include) != 1:
        raise PatchError("unable to locate unique WebServer include span")
    return source.replace(include, include + "\n" + _helpers() + "\n", 1)


def _validate_framework(framework):
    try:
        package = json.loads((framework / "package.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise PatchError(f"cannot read framework package metadata: {framework}") from exc
    if package.get("name") != FRAMEWORK_NAME or package.get("version") != FRAMEWORK_VERSION:
        raise PatchError("framework is not Arduino-ESP32 2.0.14 / 3.20014.231204")


def _atomic_write(path, contents):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = pathlib.Path(output.name)
        output.write(contents)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def prepare_replacement(framework, build_dir):
    framework = pathlib.Path(framework)
    build_dir = pathlib.Path(build_dir)
    _validate_framework(framework)
    source_path = framework / SOURCE_NAME
    try:
        source = source_path.read_bytes()
    except OSError as exc:
        raise PatchError(f"cannot read pinned WebServer source: {source_path}") from exc
    if hashlib.sha256(source).hexdigest() != PRISTINE_SHA256:
        raise PatchError("Parsing.cpp is not the verified pristine Arduino 2.0.14 source")
    patched = patched_source(source.decode("utf-8")).encode("utf-8")
    if hashlib.sha256(patched).hexdigest() != PATCHED_SHA256:
        raise PatchError("generated Parsing.cpp does not match the pinned patched hash")
    replacement = build_dir / "anchor-webserver" / "Parsing.cpp"
    if not replacement.is_file() or replacement.read_bytes() != patched:
        _atomic_write(replacement, patched)
    return replacement


def install_middleware(env, replacement):
    def replace_webserver_parsing(_env, _node):
        return _env.File(str(replacement))
    env.AddBuildMiddleware(replace_webserver_parsing, MIDDLEWARE_PATTERN)


def _run(env):
    framework = pathlib.Path(env.PioPlatform().get_package_dir(FRAMEWORK_NAME))
    replacement = prepare_replacement(framework, pathlib.Path(env.subst("$BUILD_DIR")))
    install_middleware(env, replacement)


try:
    Import("env")
except NameError:
    pass
else:
    _run(env)
