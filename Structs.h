#include "DNSHeader.h"
#include "DNSQuestion.h"
#include "DNSRecord.h"
using namespace std;
struct DNSQuery {
    DNSHeader header;
    DNSQuestion question;
} typedef DNSQuery;

struct Response {
    DNSHeader header;
    DNSRecord rec;
} typedef Response;

Response* send_dns_response(string host, ushort id);
