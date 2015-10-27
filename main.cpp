#include "platform.h"
#include <string>
#include <iostream>
#include <map>

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

void test_multikvs(SSDBClient &client)
{
	// multi_set
	std::map<std::string, std::string> kvs;
	kvs.insert(std::make_pair("multi_set_1", "1"));
	kvs.insert(std::make_pair("multi_set_2", "2"));
	Status s = client.multi_set(kvs);
	if (!s.ok())
	{
		std::cout << "multi set request fail" << std::endl;
	}
	std::vector<std::string> keys;
	for (std::map<std::string, std::string>::iterator iter = kvs.begin(); iter != kvs.end(); ++iter)
	{
		keys.push_back(iter->first);
	}
	keys.push_back("key_not_exist");
	std::map<std::string, std::string> values;
	s = client.multi_get(keys, &values);
	if (!s.ok() || s.not_found())
	{
		std::cout << "multi get request fail" << std::endl;
	}
	for (size_t i = 0; i < keys.size(); i++)
	{
		if (kvs[keys[i]] != values[keys[i]])
		{
			std::cout << "multi set value fail " << keys[i] << " " << kvs[keys[i]] << " " << values[keys[i]] << std::endl;
			return;
		}
	}
	s = client.multi_del(keys);
	if (!s.ok())
	{
		std::cout << "multi del request fail" << std::endl;
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

void test_multihash(SSDBClient &client)
{
	std::string name("test_multihash");
	std::map<std::string, std::string> kvs;
	kvs.insert(std::make_pair("hash1", "5"));
	kvs.insert(std::make_pair("hash2", "6"));
	kvs.insert(std::make_pair("hash3", "7"));
	Status s = client.multi_hset(name, kvs);
	if (!s.ok())
	{
		std::cout << "multi hset request fail" << std::endl;
		return;
	}
	std::map<std::string, std::string> values;
	std::vector<std::string> keys;
	for (std::map<std::string, std::string>::iterator iter = kvs.begin(); iter != kvs.end(); ++iter)
	{
		keys.push_back(iter->first);
	}
	s = client.multi_hget(name, keys, &values);
	if (!s.ok())
	{
		std::cout << "multi hget request fail" << std::endl;
		return;
	}
	for (size_t i = 0; i < keys.size(); i++)
	{
		if (kvs[keys[i]] != values[keys[i]])
		{
			std::cout << "multi hset value fail " << keys[i] << " " << kvs[keys[i]] << " " << values[keys[i]] << std::endl;
			return;
		}
	}
}

int main()
{	
	SSDBClient client;
	client.connect("203.116.50.232", 8888);
	test_kv(client);
	test_multikvs(client);
	test_hash(client);
	test_multihash(client);

    return 0;
}