#ifndef HEARTEN_HTTP_HTTP_H_
#define HEARTEN_HTTP_HTTP_H_

#include <string>
#include <unordered_map>
#include <sstream>
#include <iostream>

#include "socket/servernet.h"

namespace hearten {

namespace detail {

std::string descript(int status) {
  switch (status) {
    case 100: return "Continue";
    case 101: return "Switching Protocol";
    case 102: return "Processing";
    case 103: return "Early Hints";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";
    case 300: return "Multiple Choice";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 306: return "unused";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Entity";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";

    default:
    case 500: return "Internal Server Error";
  }
}

std::string filetype(const std::string& file) {
  if (file == ".css") return "text/css; charset=utf-8";
  if (file == ".js") return "application/x-javascript; charset=utf-8";
  if (file == ".jpg") return "application/x-jpg";
  if (file == ".ico") return "application/x-icon";
  return "text/plain";
}

class Request {
public:
  Request(const std::string& request) {
    size_t curpos = 0;
    auto start_line_endpos =  request.find("\r\n");
    parseStartLine(request.substr(curpos, start_line_endpos));

    curpos = start_line_endpos + 2;
    auto header_endpos = request.find("\r\n\r\n", start_line_endpos);
    parseHeader(request.substr(curpos, header_endpos - curpos));

    curpos = header_endpos + 4;
    parseBody(request.substr(curpos));
  }

  std::string getMethod() const { return method_; }
  std::string getUrl() const { return url_; }
  std::string getProtocol() const { return protocol_; }
  std::string getHeader(const std::string& key) { return head_field_[key]; }

private:
  void parseStartLine(std::string&& start_line) {
    auto urlpos = start_line.find_first_of('/');
    method_ = start_line.substr(0, urlpos - 1);

    auto protocolpos = start_line.find("HTTP/", urlpos);
    url_ = start_line.substr(urlpos, protocolpos - urlpos - 1);

    protocol_ = (start_line.substr(protocolpos));
  }

  void parseHeader(std::string&& header) {
    size_t curpos = 0;
    for (auto endpos = header.find("\r\n");
         endpos != std::string::npos;
         curpos = endpos + 2, endpos = header.find("\r\n", curpos)) {
      auto field = header.substr(curpos, endpos - curpos);
      auto colonpos = field.find_first_of(':');
      head_field_[field.substr(0, colonpos)] = field.substr(colonpos + 1);
    }
  }

  void parseBody(std::string&& body) {
    body_ = std::move(body);
  }

  std::string method_;
  std::string url_;
  std::string protocol_;
  std::unordered_map<std::string, std::string> head_field_;
  std::string body_;
};

class Response {
public:
  Response& setProtocol(std::string protocol)
  { protocol_ = std::move(protocol); return *this; }
  Response& setStatus(int status)
  { status_ = status; return *this; }
  Response& setHeader(std::string key, std::string value)
  { head_field_.emplace(std::move(key), std::move(value)); return *this; }

  Response& sendfile(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) return *this;
    std::stringstream os;
    os << file.rdbuf();
    body_.assign(os.str());
    return *this;
  }
  Response& send(std::string message)
  { body_ = std::move(message); return *this; }

  std::string toResponse() const {
    std::ostringstream os;
    os << protocol_ << ' ' << status_ << ' ' << descript(status_) << "\r\n";
    for (auto && item : head_field_)
      os << item.first << ':' << item.second << "\r\n";
    os << "\r\n" << body_;
    return os.str();
  }

private:
  std::string protocol_;
  int status_;
  std::unordered_map<std::string, std::string> head_field_;
  std::string body_;
};

class Interceptor {
public:
  using Callback = std::function<void(Request&, Response&)>;
  Interceptor& intercept(std::string url, Callback cb) {
    intercept_path_.emplace_back(std::move(url), std::move(cb));
    return *this;
  }
  bool goThrough(Request& req, Response& res) {
    bool has_intercept = false;
    std::string url = req.getUrl();
    for (auto && item : intercept_path_) {
      if (item.first == url) {
        item.second(req, res);
        has_intercept = true;
      }
    }
    return has_intercept;
  }

private:
  std::vector<std::pair<std::string, Callback>> intercept_path_;
};

} // namespace detail

class Http {
public:
  enum Method { GET, POST, PUT, DELETE };
  Http& setResourcePath(std::string resource_path) {
    if (resource_path.back() == '/') resource_path.pop_back();
    resource_path_ = std::move(resource_path);
    return *this;
  }

  Http& on(Method method, std::string url, detail::Interceptor::Callback cb) {
    interceptors_[method].intercept(std::move(url), std::move(cb));
    return *this;
  }
  template<typename Ip>
  void listen(Ip&& ip, uint16_t port) {
    IOEventLoop loop;
    ServerNet server{detail::IPv4Addr(std::forward<Ip>(ip), port), loop};
    server.setMessageCallback([this](auto handle) { onMessage(handle); });
    server.start();
    loop.loop();
  }

private:
  void onMessage(detail::Connection::Handle& handle) {
    detail::Request req{handle.read()};
    detail::Response res;

    res.setProtocol(req.getProtocol());

    auto dotpos = req.getUrl().find_last_of('.');
    std::string file_type;
    if (dotpos == std::string::npos) file_type = "text/html; charset=utf-8";
    else file_type = detail::filetype(req.getUrl().substr(dotpos));
    res.setHeader("Content-Type", std::move(file_type));

    if (!interceptors_[toMethodEnum(req.getMethod())].goThrough(req, res)) {
      std::string path{resource_path_ + req.getUrl()};
      if (req.getUrl() == "/") path += "index.html";
      DEBUG << path;
      std::ifstream file{path, std::ios::binary};
      if (!file.is_open()) res.setStatus(404);
      else {
        res.setStatus(200);
        std::stringstream os;
        os << file.rdbuf();
        res.send(os.str());
      }
      handle.send(res.toResponse());
    }
    handle.shutdown();
  }
  Method toMethodEnum(const std::string& method) {
    if (method == "POST") return POST;
    if (method == "PUT") return PUT;
    if (method == "DELETE") return DELETE;
    return GET;
  }

  std::string resource_path_;
  std::unordered_map<Method, detail::Interceptor> interceptors_;
};

} // namespace hearten

#endif
