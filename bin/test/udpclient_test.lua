package.path = package.path..";../?.lua"
package.path = package.path..";../common/?.lua"
package.path = package.path..";../common/hotfix/?.lua"
package.path = package.path..";../common/lua-protobuf/?.lua"
package.path = package.path..";../common/lua-xml/?.lua"
package.path = package.path..";../config/?.lua"
package.path = package.path..";../proto/?.lua"

local util = require "util"

-- lua ������
local data = {
   name = "ilse",
   age  = 18,
   contacts = {
      { name = "alice", phonenumber = 12312341234 },
      { name = "bob",   phonenumber = 45645674567 }
   }
}

client = udpclient.new()

fun_connect = function(conn_idx, is_success)
    if is_success then    
        -- (1)�����ɶ�����
        byte, len = seri.pack(data)
        client:send_msg(conn_idx, byte, len)
    end
end

fun_close = function(conn_idx)
	print("onclose" .. conn_idx)
end

fun_recv = function(conn_idx, data, len)
    -- (2)������Lua��
	local t = assert(seri.unpack(data, len))
    dump(t)
    client:send_msg(conn_idx, data, len);
end

for i=1, 1, 1 do
    client:connect("127.0.0.1", 12345, fun_connect, fun_close, fun_recv, 8192, 8192) 
end