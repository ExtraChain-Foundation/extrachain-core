#include "datastorage/ip_parser/countryblockipv4.h"
#include <QDebug>
#include <QFile>

CountryBlockIPv4::IP::IP(const std::string &ipAddress,
                                 const int &geoname_id,
                                 const int &registered_geoname_id)
    : ipAddress(ipAddress), geoname_id(geoname_id),
      registered_geoname_id(registered_geoname_id) {}

const std::vector<CountryBlockIPv4::IP> &CountryBlockIPv4::ipsData() const {
  return _ipsData;
}

bool CountryBlockIPv4::isParsed() { return !_ipsData.empty(); }

CountryBlockIPv4::CountryBlockIPv4(const std::string &file,
                                   ParserIPCountryLocationCSV &parserIPCountry)
    : _file(file), _parserIPCountryLocationCSV(parserIPCountry) {
  parse();
}

void CountryBlockIPv4::parse() {
    if(_file.empty())
        qFatal("CountryBlockIPv4 path to location file is empty.");

  QFile file(QString::fromStdString(_file));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    while (!file.atEnd()) {
      auto list = file.readLine().split(',').toList().toVector();
      IP ipData(list.first().split('/').first().toStdString(),
                    list[1].toInt(), list[2].toInt());
      _ipsData.push_back(ipData);
    }
  }
}

CountryBlockIPv4::IP
CountryBlockIPv4::search(const std::string &ipAddress) {
  if (!isParsed())
    qFatal("Data is empty. Please set actual path to file.");

  auto it = std::find_if(
      _ipsData.begin(), _ipsData.end(),
      [ipAddress](const IP &d) { return d.ipAddress == ipAddress; });
  it->print();
  CountryData countryData =
      _parserIPCountryLocationCSV.search(it->geoname_id);
  qDebug() << "Search by ip address: ";
  countryData.print();
  return *it;
}

std::vector<CountryBlockIPv4::IP>
CountryBlockIPv4::searchByCountry(const std::string &countryName) {
  if (!isParsed())
    qFatal("Data is empty. Please set actual path to file.");
  std::vector<IP> result;
  CountryData countryData =
      _parserIPCountryLocationCSV.searchByCountryName(countryName);
  for (const auto &data : _ipsData) {
    if (data.geoname_id == countryData.geoname_id ||
        data.registered_geoname_id == countryData.geoname_id) {
      result.push_back(data);
    }
  }
  qDebug() << "Founded " << result.size() << " by "
           << QString::fromStdString(countryName) << " name."
           << "Geoname id " << countryData.geoname_id;
  return result;
}

void CountryBlockIPv4::IP::print() {
  qDebug() << "IP address: " << QString::fromStdString(ipAddress)
           << ". Country geoname id:" << geoname_id
           << ". Registered country geoname id:" << registered_geoname_id
           << ".";
}
