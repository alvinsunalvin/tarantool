test_run = require('test_run').new()
test_run:cmd("setopt delimiter ';'")

net_box = require('net.box')
msgpack = require('msgpack')
urilib = require('uri')

IPROTO_REQUEST_TYPE   = 0x00
IPROTO_PREPARE        = 13
IPROTO_SQL_TEXT       = 0x40
IPROTO_STMT_ID        = 0x43

function receive_response(socket)
    local size = socket:read(5)
    assert(size ~= nil, 'Failed to read response')
    size = msgpack.decode(size)
    local response = socket:read(size)
    local header, header_len = msgpack.decode(response)
    local body = msgpack.decode(response:sub(header_len))
    return {
        ['header'] = header,
        ['body'] = body
    }
end

function test_request(socket, query_header, query_body)
    local header = msgpack.encode(query_header)
    local body = msgpack.encode(query_body)
    local size = msgpack.encode(header:len() + body:len())
    assert(socket:write(size .. header .. body) ~= nil,
           'Failed to send request')
    return receive_response(socket)
end

test_run:cmd("setopt delimiter ''");

box.schema.user.grant('guest', 'read, write, execute', 'universe')
uri = urilib.parse(box.cfg.listen)
socket = net_box.establish_connection(uri.host, uri.service)

--
-- gh-4769: Unprepare response must have a body.
--
header = { [IPROTO_REQUEST_TYPE] = IPROTO_PREPARE }
body = { [IPROTO_SQL_TEXT] = 'SELECT 1' }
response = test_request(socket, header, body)

body = { [IPROTO_STMT_ID] = response['body'][IPROTO_STMT_ID] }
response = test_request(socket, header, body)
response.body

box.schema.user.revoke('guest', 'read, write, execute', 'universe')
socket:close()

