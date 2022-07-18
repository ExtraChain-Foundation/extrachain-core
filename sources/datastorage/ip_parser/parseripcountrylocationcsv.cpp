#include "datastorage/ip_parser/parseripcountrylocationcsv.h"
#include <QTextStream>
#include <QFile>
#include <QDebug>

ParserIPCountryLocationCSV::ParserIPCountryLocationCSV(const std::string &locationFile)
    : _locationFile(locationFile) {
    parse();
}

void ParserIPCountryLocationCSV::parse() {
    if(_locationFile.empty())
        qFatal("ParserIPCountryLocationCSV path to location file is empty.");

    QFile file(_locationFile.c_str());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            auto list = file.readLine().split(',').toList().toVector();
            CountryData data(QStringList{list[0], QString(list[3]), list[5]});
            _countryData.push_back(data);
        }
    }
    qDebug() << "Size coutry data " << _countryData.size();

    if(!isParsed()) {
        qFatal("Data is empty. Please set actual path to file.");
    }
}

CountryData ParserIPCountryLocationCSV::search(const int &geoname_id) {
    if(!isParsed()) {
        qFatal("Data is empty. Please set actual path to file.");
    }

    auto it = std::find_if(_countryData.begin(), _countryData.end(),
                           [geoname_id](const CountryData &d) {
                               return d.geoname_id == geoname_id;
                           });
    return *it;
}

CountryData ParserIPCountryLocationCSV::searchByCountryName(const std::string &nameCountry) {
    if(!isParsed()) {
        qFatal("Data is empty. Please set actual path to file.");
    }

    for (const auto &countryData : _countryData) {
        if (countryData.country == nameCountry) {
            return countryData;
        }
    }
    return CountryData();
}

std::vector<CountryData> ParserIPCountryLocationCSV::searchByContinent(
    const std::string &nameContinent) {
    if(!isParsed()) {
        qFatal("Data is empty. Please set actual path to file.");
    }

    std::vector<CountryData> result;
    for (const auto &countryData : _countryData) {
        if (countryData.continent == nameContinent) {
            result.push_back(countryData);
        }
    }
    qDebug() << "Founded " << result.size() << " countries by continent "
             << QString::fromStdString(nameContinent) << ".";
    return result;
}

void ParserIPCountryLocationCSV::print() {
    for (const CountryData &data : _countryData) {
        data.print();
    }
}

CountryData::CountryData(QStringList listData) {
    geoname_id = listData[0].toInt();
    continent = listData[1].toStdString();
    country = listData[2].toStdString();
}

void CountryData::print() const {
    qDebug() << "Geoname id: " << geoname_id << ". Continent is "
             << QString::fromStdString(continent) << ". Country is "
             << QString::fromStdString(country);
}

const std::vector<CountryData> &ParserIPCountryLocationCSV::countryData() const
{
    return _countryData;
}

bool ParserIPCountryLocationCSV::isParsed()
{
    return !_countryData.empty();
}
