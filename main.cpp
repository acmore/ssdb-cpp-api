#include "platform.h"
#include <string>
#include <iostream>

#include "ssdb_client.h"

using namespace std;

void test_kv(SSDBClient &client)
{
	string key("test_key");
	string value("hello_ssdb");
	string expectValue;
	Status s = client.get(key, &expectValue);
	if (s.not_found())
	{
		std::cout << "setx should be fine" << std::endl;
	}
	else
	{
		std::cout << "key existed" << std::endl;
		s = client.del(key);
		if (!s.ok())
		{
			std::cout << "delete existing key failed" << std::endl;
			return;
		}
	}
	s = client.set(key, value);
	if (!s.ok())
	{
		std::cout << "set fail" << std::endl;
		return;
	}
	s = client.get(key, &expectValue);
	if (!s.ok() || s.not_found())
	{
		std::cout << "get fail" << std::endl;
		return;
	}
	if (expectValue != value)
	{
		std::cout << "get value not the same" << std::endl;
		return;
	}
	s = client.del(key);
	if (!s.ok())
	{
		std::cout << "del value fail" << std::endl;
		return;
	}
	s = client.get(key, &expectValue);
	if (!s.not_found())
	{
		std::cout << "del value fail to delete" << std::endl;
		return;
	}
	s = client.setx(key, value, 5);
	if (!s.ok())
	{
		std::cout << "setx fail" << std::endl;
		return;
	}
	expectValue.clear();
	s = client.get(key, &expectValue);
	if (!s.ok() || expectValue != value)
	{
		std::cout << "setx failed to set value" << std::endl;
		return;
	}
}

void test_hash(SSDBClient &client)
{
	string key("test_hash");
	string subkey("subkey1");
	string value("hello hash");
	Status s = client.hset(key, subkey, value);
	if (!s.ok())
	{
		std::cout << "hset fail" << std::endl;
		return;
	}
	string expectValue;
	s = client.hget(key, subkey, &expectValue);
	if (!s.ok() || s.not_found())
	{
		std::cout << "hget fail" << std::endl;
		return;
	}
	if (expectValue != value)
	{
		std::cout << "hget value not the same" << std::endl;
		return;
	}
}

int main()
{	
	SSDBClient client;
	client.connect("203.116.50.232", 8888);
	test_kv(client);
	test_hash(client);

    return 0;
}