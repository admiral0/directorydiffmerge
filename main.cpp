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

#include <iostream>
#include <fstream>
#include <functional>
#include <map>
#include <boost/program_options.hpp>
#include "diskdiff.h"

using namespace std;
using namespace std::filesystem;
using namespace boost::program_options;

bool ls(variables_map& vm)
{
    string outFileName;
    ofstream outfile;
    auto getOutputFile=[&]() -> ostream&
    {
        if(outFileName.empty()) return cout;
        outfile.open(outFileName);
        return outfile;
    };

    if(vm.count("out"))
    {
        outFileName=vm["out"].as<string>();
        if(exists(outFileName))
        {
            cerr<<"Output file "<<outFileName<<" already exists. Aborting.\n";
            return true;
        }
    }

    if(vm.count("source"))
    {
        DirectoryTree dt(vm["source"].as<string>());
        if(dt.unsupportedFilesFound()) cerr<<"Warning: unsupported files found\n";
        getOutputFile()<<dt;
        return true;
    }

    return false;
}

bool compare(variables_map& vm)
{
    string outFileName;
    ofstream outfile;
    auto getOutputFile=[&]() -> ostream&
    {
        if(outFileName.empty()) return cout;
        outfile.open(outFileName);
        return outfile;
    };

    if(vm.count("out"))
    {
        outFileName=vm["out"].as<string>();
        if(exists(outFileName))
        {
            cerr<<"Output file "<<outFileName<<" already exists. Aborting.\n";
            return true;
        }
    }

    if(vm.count("source") && vm.count("target"))
    {
        DirectoryTree a(vm["source"].as<string>());
        DirectoryTree b(vm["target"].as<string>());
        if(a.unsupportedFilesFound() || b.unsupportedFilesFound())
            cerr<<"Warning: unsupported files found\n";
        auto diff=compare2(a,b);
        getOutputFile()<<diff;
        return true;
    }

    return false;
}

bool test(variables_map&)
{
    string fileName="dump.txt";
    ifstream in(fileName);
    assert(in);
    DirectoryTree dt(in,fileName);
    cout<<dt;
    return true;
}

int main(int argc, char *argv[])
{
    options_description desc("diskdiff options");
    desc.add_options()
        ("help",     "prints this")
        ("source,s", value<string>(), "source path")
        ("target,t", value<string>(), "target path")
        ("out,o",    value<string>(), "save data to arg instead of stdout")
    ;

    if(argc<2)
    {
        cout<<desc<<'\n';
        return 1;
    }

    string op=argv[1]; //TODO: use program options for this
    variables_map vm;
    store(parse_command_line(argc,argv,desc),vm);
    notify(vm);

    const map<string,function<bool (variables_map&)>> operations=
    {
        {"ls", ls},
        {"compare", compare},
        {"test", test}
    };

    auto it=operations.find(op);
    if(it!=operations.end() && it->second(vm)) return 0;

    //No valid option passed, print help
    cout<<desc<<'\n';
    return 1;
}
