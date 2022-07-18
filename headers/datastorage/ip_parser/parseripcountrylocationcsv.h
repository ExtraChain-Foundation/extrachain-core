#ifndef PARSERIPCOUNTRYLOCATIONCSV_H
#define PARSERIPCOUNTRYLOCATIONCSV_H
#include <QString>
#include <string>
#include <vector>

struct CountryData {
    CountryData() {};
    CountryData(QStringList listData);

    void print() const;

    int geoname_id = -1;
    std::string continent;
    std::string country;
};

class ParserIPCountryLocationCSV {
    std::string _locationFile;
    std::vector<CountryData> _countryData;

public:
    ParserIPCountryLocationCSV(const std::string &locationFile);
    void parse();
    CountryData search(const int &geoname_id);
    CountryData searchByCountryName(const std::string &nameCountry);
    std::vector<CountryData> searchByContinent(const std::string &nameContinent);

    void print();
    const std::vector<CountryData> &countryData() const;
    bool isParsed();
};

#endif // PARSERIPCOUNTRYLOCATIONCSV_H
