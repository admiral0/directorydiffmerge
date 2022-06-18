/***************************************************************************
 *   Copyright (C) 2022 by Terraneo Federico                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#include <list>
#include <sstream>
#include <cassert>
#include <iomanip>
#include <crypto++/sha.h>
#include <crypto++/hex.h>
#include <crypto++/files.h>
#include <crypto++/filters.h>
#include "extfs.h"
#include "diskdiff.h"

using namespace std;
using namespace std::filesystem;

string hashFile(const path& p)
{
    using namespace CryptoPP;
    SHA1 hash;
    string result;
    FileSource fs(p.string().c_str(),true,
                  new HashFilter(hash,new HexEncoder(new StringSink(result))));
    return result;
}

//
// class FilesystemElement
//

FilesystemElement::FilesystemElement()
    : ty(file_type::unknown), per(perms::unknown) {}

FilesystemElement::FilesystemElement(const path& p, const path& top)
    : rp(p.lexically_relative(top))
{
    auto s=ext_symlink_status(p);
    per=s.permissions();
    us=s.user();
    gs=s.group();
    mt=s.mtime();
    ty=s.type();
    hardLinkCnt=s.hard_link_count();
    switch(s.type())
    {
        case file_type::regular:
            sz=s.file_size();
            fileHash=hashFile(p);
            break;
        case file_type::directory:
            break;
        case file_type::symlink:
            symlink=read_symlink(p);
            break;
        default:
            ty=file_type::unknown; //We don't handle other types
    }
}

void FilesystemElement::readFrom(const string& diffLine,
                                 const string& diffFileName, int lineNo)
{
    auto fail=[&](const string& m)
    {
        string s=diffFileName;
        if(diffFileName.empty()==false) s+=": ";
        s+=m;
        if(lineNo>0) s+=" at line"+to_string(lineNo);
        s+=", wrong line is '"+diffLine+"'";
        throw runtime_error(s);
    };

    istringstream in(diffLine);
    string permStr;
    in>>permStr;
    if(!in || permStr.size()!=10) fail("Error reading permission string");
    switch(permStr.at(0))
    {
        case '-': ty=file_type::regular;   break;
        case 'd': ty=file_type::directory; break;
        case 'l': ty=file_type::symlink;   break;
        case '?': ty=file_type::unknown;   break;
        default: fail("Unrecognized file type");
    }
    int pe=0;
    for(int i=0;i<3;i++)
    {
        string permTriple=permStr.substr(3*i+1,3);
        assert(permTriple.size()==3);
        pe<<=3;
        if(permTriple[0]=='r') pe |= 0004;
        else if(permTriple[0]!='-') fail("Permissions not correct");
        if(permTriple[1]=='w') pe |= 0002;
        else if(permTriple[1]!='-') fail("Permissions not correct");
        if(permTriple[2]=='x') pe |= 0001;
        else if(permTriple[2]!='-') fail("Permissions not correct");
    }
    per=static_cast<perms>(pe);
    in>>us>>gs;
    if(!in) fail("Error reading user/group");
    // Time is complicated. The format string "%F %T" always causes the stream
    // fail bit to be set. But expanding %F as %Y-%m-%d works, go figure.
    // Additionally, trying to add %z to parse time zone always fails. After all
    // there's no field for the time zone in struct tm, so where exactly is
    // get_time expected to put that data? Since time zone correction would have
    // to be done with custom code and there may be corner cases I'm not aware
    // of, I decided to only support UTC and check the +0000 string manually
    struct tm t;
    in>>get_time(&t,"%Y-%m-%d %T");
    mt=timegm(&t);
    if(!in || mt==-1) fail("Error reading mtime");
    string tz;
    tz.resize(6);
    in.read(tz.data(),6);
    if(!in || tz!=" +0000") fail("Error reading mtime");
    switch(ty)
    {
        case file_type::regular:
            in>>sz;
            if(!in) fail("Error reading size");
            in>>fileHash;
            if(!in || fileHash.size()!=40) fail("Error reading hash");
            break;
        case file_type::symlink:
            in>>symlink;
            if(!in) fail("Error reading symlink target");
            break;
    }
    in>>rp;
    if(!in) fail("Error reading path");
    if(in.get()!=EOF) fail("Extra characters at end of line");
    //Initialize non-written fields to defaults
    hardLinkCnt=1;
}

void FilesystemElement::writeTo(ostream& os)
{
    switch(ty)
    {
        case file_type::regular:   os<<'-'; break;
        case file_type::directory: os<<'d'; break;
        case file_type::symlink:   os<<'l'; break;
        default:                   os<<'?'; break;
    }
    int pe=static_cast<int>(per);
    os<<(pe & 0400 ? 'r' : '-')
      <<(pe & 0200 ? 'w' : '-')
      <<(pe & 0100 ? 'x' : '-')
      <<(pe & 0040 ? 'r' : '-')
      <<(pe & 0020 ? 'w' : '-')
      <<(pe & 0010 ? 'x' : '-')
      <<(pe & 0004 ? 'r' : '-')
      <<(pe & 0002 ? 'w' : '-')
      <<(pe & 0001 ? 'x' : '-');
    os<<' '<<us<<' '<<gs<<' ';
    // Time is complicated. The gmtime_r functions, given its name, should fill
    // a struct tm with GMT time, but the documentation says UTC. And it's
    // unclear how it handles leap seconds, that should be the difference
    // between GMT and UTC. Nobody on the Internet appears to know exactly, but
    // it appears to be OS dependent.
    // Additionally put_time has a format string %z to print the time zone, but
    // a struct tm has no fields to encode the time zone, so where does put_time
    // take the time zone information that it prints? Not sure.
    // So I decided to print +0000 manually as a string to be extra sure
    struct tm t;
    assert(gmtime_r(&mt,&t)==&t);
    os<<put_time(&t,"%F %T +0000")<<' ';
    switch(ty)
    {
        case file_type::regular:
            os<<sz<<' '<<fileHash<<' ';
            break;
        case file_type::symlink:
            os<<symlink<<' ';
            break;
    }
    os<<rp<<'\n';
}

bool operator< (const FilesystemElement& a, const FilesystemElement& b)
{
    // Sort alphabetically (case sensitive) but put directories first
    if(a.isDirectory()==b.isDirectory()) return a.relativePath() < b.relativePath();
    return a.isDirectory() > b.isDirectory();
}

//
// class FileLister
//

void FileLister::listFiles(const path& top)
{
    this->top=absolute(top);
    if(!is_directory(this->top))
        throw logic_error(top.string()+" is not a directory");
    printBreak=false;
    unsupported=false;
    recursiveListFiles(this->top);
}

void FileLister::recursiveListFiles(const path& p)
{
    if(printBreak) os<<'\n';

    list<FilesystemElement> fe;
    for(auto& it : directory_iterator(p))
        fe.push_back(FilesystemElement(it.path(),top));
    fe.sort();
    for(auto& e : fe)
    {
        e.writeTo(os);
        if(e.type()==file_type::unknown)
        {
            cerr<<"Warning: "<<e.relativePath()<<" has unsupported file type\n";
            unsupported=true;
        }
        if(e.type()!=file_type::directory && e.hardLinkCount()!=1)
        {
            cerr<<"Warning: "<<e.relativePath()<<" has multiple hardlinks ("<<e.hardLinkCount()<<")\n";
            unsupported=true;
        }
    }
    printBreak=fe.empty()==false;

    for(auto& e : fe)
    {
        //NOTE: we list directories, not symlinks to directories. This also
        //saves us from worrying about filesystem loops through directory symlinks.
        if(e.isDirectory()) recursiveListFiles(top / e.relativePath());
    }
}
