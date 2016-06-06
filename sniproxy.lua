function hex_dump(buf)
  for byte=1, #buf, 16 do
    local chunk = buf:sub(byte, byte+15)
    io.write(string.format('%08X  ',byte-1))
    chunk:gsub('.', function (c) io.write(string.format('%02X ',string.byte(c))) end)
    io.write(string.rep(' ',3*(16-#chunk)))
    io.write("\n") 
  end
end

function preconnect(remote, name)
  print('->')
  hex_dump(remote)
  print(name)
  if remote == string.char(127,0,0,1) and name == 'rss.7bits.nl' then
    print('blocking')
    return true
  end
  print('--')
  return false
end
