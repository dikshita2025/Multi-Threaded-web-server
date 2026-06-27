#include "http/HttpParser.h"
#include <sstream>
#include <iostream>
#include <algorithm>

HttpParser::HttpParser() {
    reset();
}

void HttpParser::reset() {
    state_ = State::REQUEST_LINE;
    buffer_.clear();
    body_bytes_read_ = 0;
    content_length_ = 0;
}

// Helper to decode percent-encoded URIs (e.g. "%20" -> " ")
static std::string url_decode(const std::string& src) {
    std::string ret;
    char ch;
    int ii;
    for (size_t pos = 0; pos < src.length(); ++pos) {
        if (src[pos] == '%') {
            if (pos + 2 < src.length() && 
                std::sscanf(src.substr(pos + 1, 2).c_str(), "%x", &ii) == 1) {
                ch = static_cast<char>(ii);
                ret += ch;
                pos += 2;
            } else {
                ret += src[pos];
            }
        } else if (src[pos] == '+') {
            ret += ' ';
        } else {
            ret += src[pos];
        }
    }
    return ret;
}

HttpParser::ParseStatus HttpParser::parse(HttpRequest& request, const char* data, size_t length) {
    buffer_.append(data, length);
    return parse_loop(request);
}

HttpParser::ParseStatus HttpParser::parse_loop(HttpRequest& request) {
    while (state_ != State::COMPLETE && state_ != State::ERROR) {
        if (state_ == State::REQUEST_LINE || state_ == State::HEADERS) {
            size_t pos = buffer_.find("\r\n");
            if (pos == std::string::npos) {
                return ParseStatus::INCOMPLETE;
            }

            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 2); // remove line and CRLF

            if (state_ == State::REQUEST_LINE) {
                if (parse_request_line(request, line) == ParseStatus::ERROR) {
                    state_ = State::ERROR;
                } else {
                    state_ = State::HEADERS;
                }
            } else { // State::HEADERS
                if (line.empty()) { // End of headers
                    std::string cl_str = request.get_header("Content-Length");
                    if (!cl_str.empty()) {
                        try {
                            content_length_ = std::stoul(cl_str);
                        } catch (...) {
                            state_ = State::ERROR;
                            break;
                        }
                    }
                    
                    if (content_length_ > 0) {
                        state_ = State::BODY;
                    } else {
                        state_ = State::COMPLETE;
                    }
                } else {
                    if (parse_header(request, line) == ParseStatus::ERROR) {
                        state_ = State::ERROR;
                    }
                }
            }
        } else if (state_ == State::BODY) {
            if (buffer_.size() < content_length_) {
                return ParseStatus::INCOMPLETE;
            }

            request.set_body(buffer_.substr(0, content_length_));
            buffer_.erase(0, content_length_);
            state_ = State::COMPLETE;
        }
    }

    if (state_ == State::COMPLETE) {
        return ParseStatus::COMPLETE;
    }
    return ParseStatus::ERROR;
}

HttpParser::ParseStatus HttpParser::parse_request_line(HttpRequest& request, const std::string& line) {
    std::istringstream iss(line);
    std::string method, raw_path, version;
    if (!(iss >> method >> raw_path >> version)) {
        return ParseStatus::ERROR;
    }

    request.set_method(method);
    request.set_version(version);

    size_t q = raw_path.find('?');
    if (q != std::string::npos) {
        request.set_path(url_decode(raw_path.substr(0, q)));
        request.set_query_string(url_decode(raw_path.substr(q + 1)));
    } else {
        request.set_path(url_decode(raw_path));
        request.set_query_string("");
    }

    return ParseStatus::COMPLETE;
}

HttpParser::ParseStatus HttpParser::parse_header(HttpRequest& request, const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return ParseStatus::ERROR;
    }

    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    // Trim key and value whitespaces
    auto trim = [](std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    };
    trim(key);
    trim(value);

    if (key.empty()) {
        return ParseStatus::ERROR;
    }

    request.add_header(key, value);
    return ParseStatus::COMPLETE;
}
