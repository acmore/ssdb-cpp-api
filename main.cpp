#include "platform.h"
#include <string>
#include <iostream>

#include "ssdb_client.h"

using namespace std;

int main()
{
    cout << "fuck" << endl;
	
	SSDBClient client;
	client.connect("203.116.50.232", 8888);
	Status s = client.set("abc", "cdefg");
	if (!s.ok())
	{
		cout << "what the hell" << std::endl;
	}
	std::string val;
	s = client.get("abc", &val);
	if (!s.ok())
	{
		cout << "not ok" << endl;
	}
	cout << val << endl;
    return 0;
}