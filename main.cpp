#include "platform.h"
#include <string>
#include <iostream>

#include "ssdb_client.h"

using namespace std;

void test_kv(SSDBClient &client)
{
	string key("test_key");
	string value("hello_ssdb");
	Status s = client.set(key, value);
	if (!s.ok())
	{
		std::cout << "set fail" << std::endl;
		return;
	}
	string expectValue;
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