#ifndef COUNTRYBLOCKIPV4_H
#define COUNTRYBLOCKIPV4_H
#include <string>
#include "parseripcountrylocationcsv.h"

class CountryBlockIPv4
{
    std::string _file;
    ParserIPCountryLocationCSV _parserIPCountryLocationCSV;

    struct IP{
        IP(const std::string& ipAddress, const int& geoname_id, const int& registered_geoname_id);

        std::string ipAddress;
        int geoname_id;
        int registered_geoname_id;

        void print();
        bool isValid();;
    };

    std::vector<IP> _ipsData;

public:
    CountryBlockIPv4(const std::string& file, ParserIPCountryLocationCSV &parserIPCountry);
    void parse();
    IP search(const std::string& ipAddress);
    std::vector<IP> searchByCountry(const std::string& countryName);
    const std::vector<IP> &ipsData() const;
    bool isParsed();
};

#endif // COUNTRYBLOCKIPV4_H
