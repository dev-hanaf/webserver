# include <cerrno>
# include <climits>
# include <cstddef>
# include <cstdlib>
# include "../incs/Request.hpp"

Request::Request()
	:	_rl(""), _rh(""), _rb(), _state(BEGIN), _statusCode(START), _buffer(), _requestDone(false)
{
}

bool	Request::_processBodyHeaders()
{
	std::string contentLengthStr = _rh.getHeaderValue("content-length");
	std::string transferEncoding = _rh.getHeaderValue("transfer-encoding");

	if (!contentLengthStr.empty())
	{
		if (!_processContentLength())
			return false;
	}
	else if (!transferEncoding.empty())
	{
		_rb.setChunked(_isChunkedTransferEncoding(transferEncoding));
		if (_rb.isChunked() && contentLengthStr.empty())
			return true;
		return setState(false, BAD_REQUEST);
	}
	std::string contentType = _rh.getHeaderValue("content-type");
	if (!contentType.empty())
		_rb.setContentType(contentType);
		
	return true;
}

bool	Request::_processContentLength()
{
	std::string	contentLengthStr = _rh.getHeaderValue("content-length");
	if (contentLengthStr.empty())
		return true;

	char* end;
	unsigned long long contentLength = strtoull(contentLengthStr.c_str(), &end, 10);

	if (*end != '\0' || contentLengthStr.empty())
		return setState(false, BAD_REQUEST);

	if (contentLength > MAX_BODY_SIZE || (contentLength == ULLONG_MAX && errno == ERANGE))
		return setState(false, PAYLOAD_TOO_LARGE);

	if (contentLength == 0 && (_rl.getMethod() == "GET" || _rl.getMethod() == "DELETE"))
		_state = COMPLETE;

	_rb.setContentLength(contentLength);
	return true;
}

bool	Request::_processChunkedTransfer()
{
	std::string	transferEncoding = _rh.getHeaderValue("content-length");
	if (transferEncoding.empty())
		return false;

	_rb.setChunked(_isChunkedTransferEncoding(transferEncoding));
	if (!_rb.isChunked())
		return false;
	return true;
}

bool	Request::_validateMethodBodyCompatibility()
{
	const std::string& method = _rl.getMethod();
	bool hasBody = _rb.getContentLength() > 0 || _rb.isChunked();

	if (method == "GET" && hasBody)
		return setState(false, BAD_REQUEST);

	if (method == "POST" && !hasBody)
		return setState(false, LENGTH_REQUIRED);

	if (method == "DELETE" && hasBody &&
	   (_rb.isChunked() && _rb.getContentLength() > 0))
		return setState(false, BAD_REQUEST);

	return true;
}

bool	Request::_isChunkedTransferEncoding(const std::string& transferEncoding)
{
	if (transferEncoding.empty())
		return false;

	size_t chunkedPos = transferEncoding.rfind("chunked");
	if (chunkedPos == std::string::npos)
		return false;

	std::string afterChunked = transferEncoding.substr(chunkedPos + 7);
	if (afterChunked.find_first_not_of(" ,\t\r\n") != std::string::npos)
		return false;

	return true;
}

void	Request::clear()
{
	_rl.clear();
	_rh.clear();
	_rb.clear();
	_state = BEGIN;
	_buffer.clear();
	_statusCode = START;
}

bool	Request::stateChecker() const
{
	HttpStatusCode	curr = getStatusCode();
	if (curr == OK || curr == START)
		return true;

	return false;
}

bool	Request::isRequestDone() const
{
	if (_state == COMPLETE || _state == ERROR)
		return true;
	return false;
}

const RequestState&	Request::getState() const
{
	return _state;
}

const HttpStatusCode&	Request::getStatusCode() const
{
	return _statusCode;
}

const RequestLine&	Request::getRequestLine() const
{
	return _rl;
}

const RequestBody&	Request::getRequestBody() const
{
	return _rb;
}

const RequestHeaders&	Request::getRequestHeaders() const
{
	return _rh;
}

bool	Request::setState(bool tof, HttpStatusCode code)
{
	_statusCode = code;

	if (stateChecker() == false)
		_state = ERROR;

	return tof;
}

bool	Request::lineSection()
{
	size_t crlf_pos = _buffer.find(CRLF);

	if (crlf_pos == std::string::npos)
		return true;

	_rl = RequestLine(_buffer.substr(0, crlf_pos));
	if (!_rl.parse())
		return setState(false, _rl.getStatusCode());

	_buffer.erase(0, crlf_pos + 2);
	_state = HEADERS;
	return true;
}

bool	Request::headerSection()
{
	size_t end_header = _buffer.find(END_HEADER);
	if (end_header == std::string::npos)
		return true;

	std::string headersStr = _buffer.substr(0, end_header + 2);
	if (headersStr.empty())
		return setState(false, BAD_REQUEST);

	_rh = RequestHeaders(headersStr);
	if (!_rh.parse())
		return setState(false, _rh.getStatusCode());

	_buffer.erase(0, end_header + 4);
	if (!_processBodyHeaders())
		return false;

	if (!_validateMethodBodyCompatibility())
		return false;

	const std::string& method = _rl.getMethod();
	if ((method == "GET" || (method == "DELETE" && _rb.getContentLength() == 0 && !_rb.isChunked()))
		&& _buffer.empty())
	{
		_state = COMPLETE;
		return setState(true, OK);
	}
	_state = BODY;
	return true;
}

bool	Request::bodySection()
{
	if (!_buffer.empty())
	{
		if (!_rb.receiveData(_buffer.c_str(), _buffer.size()))
			return setState(false, _rb.getStatusCode());
		_buffer.clear();
	}

	if (_rb.isCompleted() || _buffer.empty())
	{
		if (!_rb.parse())
			return setState(false, _rb.getStatusCode());

		_state = COMPLETE;
		return setState(true, OK);
	}
	return true;
}

// TODO: TIMEOUT CHECK FROM CORE SERVER.

bool	Request::appendToBuffer(const char* data, size_t len)
{
	_buffer.append(data, len);

	bool progress = true;
	while (progress && !isRequestDone())
	{
		progress = false;
		switch (_state)
		{
			case BEGIN:
				if (!_buffer.empty())
				{
					_state = LINE;
					progress = true;
				}
				break;

			case LINE:
				if (lineSection())
					progress = true;
				break;

			case HEADERS:
				if (headerSection())
					progress = true;
				break;

			case BODY:
				if (bodySection())
					progress = true;
				break;

			case COMPLETE:
				return true;
			default:
				break;
		}
	}

	return true;
}
