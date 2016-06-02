//===========================================
//  Lumina-DE source code
//  Copyright (c) 2016, Ken Moore
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
#include "RSSObjects.h"
#include <QNetworkRequest>
#include <QXmlStreamReader>

#include "LSession.h"

//============
//    PUBLIC
//============
RSSReader::RSSReader(QObject *parent, QString settingsPrefix) : QObject(parent){
  NMAN = new QNetworkAccessManager(this);
  connect(NMAN, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)) );

  setprefix = settingsPrefix;
  syncTimer = new QTimer(this);
    syncTimer->setInterval(300000); //5 minutes
    connect(syncTimer, SIGNAL(timeout()), this, SLOT(checkTimes()));
  syncTimer->start();
}

RSSReader::~RSSReader(){

}

//Information retrieval
QStringList RSSReader::channels(){
  QStringList urls = hash.keys();
  QStringList ids;
  //sort all the channels by title before output
  for(int  i=0; i<urls.length(); i++){
    QString title = hash[urls[i]].title;
    if(title.isEmpty()){ title = "ZZZ"; } //put currently-invalid ones at the end of the list
    ids << title+" : "+urls[i];
  }
  ids.sort();
  //Now strip off all the titles again to just get the IDs
  for(int i=0; i<ids.length(); i++){
    ids[i] = ids[i].section(" : ",-1);
  }
  return ids;
}

RSSchannel RSSReader::dataForID(QString ID){
  if(hash.contains(ID)){ return hash[ID]; }
  else{ return RSSchannel(); }
}

//Initial setup
void RSSReader::addUrls(QStringList urls){
  //qDebug() << "Add URLS:" << urls;
  for(int i=0; i<urls.length(); i++){
    //Note: Make sure we get the complete URL form for accurate comparison later
    QString url = QUrl(urls[i]).toString();
    if(hash.contains(url)){ continue; } //already handled
    RSSchannel blank;
      blank.originalURL = url;
    hash.insert(url, blank); //put the empty struct into the hash for now
    requestRSS(url); //startup the initial request for this url
  }
  emit newChannelsAvailable();
}

void RSSReader::removeUrl(QString ID){
  if(hash.contains(ID)){ hash.remove(ID); }
  emit newChannelsAvailable();
}

//=================
//    PUBLIC SLOTS
//=================
void RSSReader::syncNow(){
  QStringList urls = hash.keys();
  for(int i=0; i<urls.length(); i++){
    requestRSS(urls[i]);
  }
}

//=================
//         PRIVATE
//=================
void RSSReader::requestRSS(QString url){
  if(!outstandingURLS.contains(url)){
    //qDebug() << "Request URL:" << url;
    NMAN->get( QNetworkRequest( QUrl(url) ) );
    outstandingURLS << url;
  }
}

//RSS parsing functions
RSSchannel RSSReader::readRSS(QByteArray bytes){
  //Note: We could expand this later to support multiple "channel"s per Feed
  //   but it seems like there is normally only one channel anyway
  //qDebug() << "Read RSS:" << bytes;
  QXmlStreamReader xml(bytes);
  RSSchannel rssinfo;
  //qDebug() << "Can Read XML Stream:" << !xml.hasError();
  if(xml.readNextStartElement()){
    //qDebug() << " - RSS Element:" << xml.name();
    if(xml.name() == "rss" && xml.attributes().value("version") =="2.0"){
      while(xml.readNextStartElement()){
        if(xml.name()=="channel"){ rssinfo = readRSSChannel(&xml); }
        else{ xml.skipCurrentElement(); }
      }
    }
  }
  if(xml.hasError()){ qDebug() << " - XML Read Error:" << xml.errorString() << "\n" << bytes; }
  return rssinfo;
}
RSSchannel RSSReader::readRSSChannel(QXmlStreamReader *rss){
  RSSchannel info;
  info.timetolive = -1;
  while(rss->readNextStartElement()){
    //qDebug() << " - RSS Element (channel):" <<rss->name();
    if(rss->name()=="item"){ info.items << readRSSItem(rss); }
    else if(rss->name()=="title"){ info.title = rss->readElementText(); }
    else if(rss->name()=="link"){ info.link = rss->readElementText(); }
    else if(rss->name()=="description"){ info.description = rss->readElementText(); }
    else if(rss->name()=="lastBuildDate"){ info.lastBuildDate = RSSDateTime(rss->readElementText()); }
    else if(rss->name()=="pubDate"){ info.lastPubDate = RSSDateTime(rss->readElementText()); }
    else if(rss->name()=="image"){ readRSSImage(&info, rss); }
    //else if(rss->name()=="skipHours"){ info.link = rss->readElementText(); }
    //else if(rss->name()=="skipDays"){ info.link = rss->readElementText(); }
    else if(rss->name()=="ttl"){ info.timetolive = rss->readElementText().toInt(); }
    else{ rss->skipCurrentElement(); }
  }
  return info;
}

RSSitem RSSReader::readRSSItem(QXmlStreamReader *rss){
  RSSitem item;
  while(rss->readNextStartElement()){
    //qDebug() << " - RSS Element (Item):" << rss->name();
    if(rss->name()=="title"){ item.title = rss->readElementText(); }
    else if(rss->name()=="link"){ item.link = rss->readElementText(); }
    else if(rss->name()=="description"){ item.description = rss->readElementText(); }
    else if(rss->name()=="comments"){ item.comments_url = rss->readElementText(); }
    else if(rss->name()=="author"){ 
      //Special handling - this field can contain both email and name
      QString raw = rss->readElementText(); 
      if(raw.contains("@")){  
        item.author_email = raw.split(" ").filter("@").first(); 
        item.author = raw.remove(item.author_email).remove("(").remove(")").simplified(); //the name is often put within parentheses after the email
      }else{ item.author = raw; }
    }
    else if(rss->name()=="guid"){ item.guid = rss->readElementText(); }
    else if(rss->name()=="pubDate"){ item.pubdate = RSSDateTime(rss->readElementText()); }
    else{ rss->skipCurrentElement(); }
  }
  return item;
}

void RSSReader::readRSSImage(RSSchannel *item, QXmlStreamReader *rss){
  while(rss->readNextStartElement()){
    //qDebug() << " - RSS Element (Image):" << rss->name();
    if(rss->name()=="url"){ item->icon_url = rss->readElementText(); }
    else if(rss->name()=="title"){ item->icon_title = rss->readElementText(); }
    else if(rss->name()=="link"){ item->icon_link = rss->readElementText(); }
    else if(rss->name()=="width"){ item->icon_size.setWidth(rss->readElementText().toInt()); }
    else if(rss->name()=="height"){ item->icon_size.setHeight(rss->readElementText().toInt()); }
    else if(rss->name()=="description"){ item->icon_description = rss->readElementText(); }
  }
  //Go ahead and kick off the request for the icon
  if(!item->icon_url.isEmpty()){ requestRSS(item->icon_url); }
}

QDateTime RSSReader::RSSDateTime(QString datetime){
  return QDateTime::fromString(datetime, Qt::RFC2822Date);
}

//=================
//    PRIVATE SLOTS
//=================
void RSSReader::replyFinished(QNetworkReply *reply){
  QString url = reply->request().url().toString();
  //qDebug() << "Got Reply:" << url;
  QByteArray data = reply->readAll();
  outstandingURLS.removeAll(url);
  if(data.isEmpty()){
    qDebug() << "No data returned:" << url;
    //see if the URL can be adjusted for known issues
    bool handled = false;
    QUrl redirecturl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if(redirecturl.isValid() && (redirecturl.toString() != url )){
      //New URL redirect - make the change and send a new request
      QString newurl = redirecturl.toString();
      qDebug() << " - Redirect to:" << newurl;
      if(hash.contains(url) && !hash.contains(newurl)){
        hash.insert(newurl, hash.take(url) ); //just move the data over to the new url
        requestRSS(newurl);
        emit newChannelsAvailable();
        handled = true;
      }      
    }
    if(!handled && hash.contains(url) ){ 
      emit rssChanged(url);
    }
    return;
  }

  if(!hash.contains(url)){ 
    //qDebug() << " - hash does not contain URL!!";
    //URL removed from list while a request was outstanding?
    //Could also be an icon fetch response
    QStringList keys = hash.keys();
    for(int i=0; i<keys.length(); i++){
      if(hash[keys[i]].icon_url == url){
        //Icon fetch response
        RSSchannel info = hash[keys[i]];
        QImage img = QImage::fromData(data);
        info.icon = QIcon( QPixmap::fromImage(img) );
        //qDebug() << "Got Icon response:" << url << data;
        //qDebug() << info.icon;
        hash.insert(keys[i], info); //insert back into the hash
        emit rssChanged(keys[i]);
        break;
      }
    }
    reply->deleteLater();
  }else{
    //RSS reply
    RSSchannel info = readRSS(data); //QNetworkReply can be used as QIODevice
    reply->deleteLater(); //clean up
    //Validate the info and announce any changes
    if(info.title.isEmpty() || info.link.isEmpty() || info.description.isEmpty()){ return; } //bad info/read
    //Update the bookkeeping elements of the info
    if(info.timetolive<=0){ info.timetolive = LSession::handle()->DesktopPluginSettings()->value(setprefix+"default_interval_minutes", 60).toInt(); }
    if(info.timetolive <=0){ info.timetolive = 60; } //error in integer conversion from settings?
    info.lastsync = QDateTime::currentDateTime(); info.nextsync = info.lastsync.addSecs(info.timetolive * 60); 
    //Now see if anything changed and save the info into the hash
    bool changed = (hash[url].lastBuildDate.isNull() || (hash[url].lastBuildDate < info.lastBuildDate) );
    bool newinfo = false;
    if(changed){ newinfo = hash[url].title.isEmpty(); } //no previous info from this URL
    info.originalURL = hash[url].originalURL; //make sure this info gets preserved across updates
    hash.insert(url, info);
    if(newinfo){ emit newChannelsAvailable(); } //new channel
    else if(changed){ emit rssChanged(url); } //update to existing channel
  }
}

void RSSReader::checkTimes(){
  if(LSession::handle()->DesktopPluginSettings()->value(setprefix+"manual_sync_only", false).toBool()){ return; }
  QStringList urls = hash.keys();
  QDateTime cdt = QDateTime::currentDateTime();
  for(int i=0; i<urls.length(); i++){
    if(hash[urls[i]].nextsync < cdt){ requestRSS(urls[i]); }
  }
}
