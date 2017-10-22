function preconnect(remote, name)
  print('->')
  print("["..remote.."]")
  print("["..name.."]")
  name = string.lower(name)
  print("["..name.."]")
  if remote == '127.0.0.1' and name == 'www.example.com' then
    print('blocking')
    return true
  end
  print('--')
  return false
end
