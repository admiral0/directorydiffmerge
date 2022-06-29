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

#pragma once

#include <filesystem>
#include <ostream>
#include <istream>
#include <list>
#include <array>
#include <optional>
#include <unordered_map>
#include <ctime>

/**
 * Computes SHA1 of a file. Only to detect changes, no crypto strength needed
 * \param p file path
 * \return a string with the SHA1 hash in ASCII hex digits
 */
std::string hashFile(const std::filesystem::path& p);

/**
 * This class is used to load/store information about files and directories
 * in diff files
 */
class FilesystemElement
{
public:
    /**
     * Empty constructor
     */
    FilesystemElement();

    /**
     * Constructor from a path
     * \param p absolute path
     * \param top top level directory, used to compute relative path
     */
    FilesystemElement(const std::filesystem::path& p,
                      const std::filesystem::path& top);

    /**
     * Constructor from string, used when reading from diff files
     * \param diffLine line of the diff file to construct the object from
     * \param diffFileName name of diff file, used for error reporting
     * \param lineNo line number of diff file, used for error reporting
     * \throws runtime_error in case of errors
     */
    FilesystemElement(const std::string& diffLine,
                      const std::string& diffFileName="", int lineNo=-1)
    {
        readFrom(diffLine,diffFileName,lineNo);
    }

    /**
     * Read from diff files
     * \param diffLine line of the diff file to construct the object from
     * \param diffFileName name of diff file, used for error reporting
     * \param lineNo line number of diff file, used for error reporting
     * \throws runtime_error in case of errors
     */
    void readFrom(const std::string& diffLine,
                  const std::string& diffFileName="", int lineNo=-1);

    /**
     * Write the object to an ostream based on the diff file format
     * \param os ostream where to write
     */
    void writeTo(std::ostream& os) const;

    /**
     * \return the type of the FilesystemElement (reguler file, directory, ...)
     */
    std::filesystem::file_type type() const { return ty; }

    /**
     * \return the access permissions
     */
    std::filesystem::perms permissions() const { return per; }

    /**
     * \return the owner (user) of the FilesystemElement as a string
     */
    std::string user() const { return us; }

    /**
     * \return the group of the FilesystemElement as a string
     */
    std::string group() const { return gs; }

    /**
     * \return the last modified time
     */
    time_t mtime() const { return mt; }

    /**
     * \return the file size.
     * Only valid if the FilesystemElement is a regular file
     */
    off_t size() const { return sz; }

    /**
     * \return the file hash.
     * Only valid if the FilesystemElement is a regular file
     */
    std::string hash() const { return fileHash; }

    /**
     * \return the path of the FilesystemElement, relative to the top directory
     */
    std::filesystem::path relativePath() const { return rp; }

    /**
     * \return the symlink target.
     * Only valid if the FilesystemElement is a symlink
     */
    std::filesystem::path symlinkTarget() const { return symlink; }

    /**
     * \return the hardlink count. Note that this information is not saved
     * in the diff file, so it is only available if the FilesystemElement has
     * been read from disk.
     */
    uintmax_t hardLinkCount() const { return hardLinkCnt; }

    /**
     * \return true if the FilesystemElement is a directory
     */
    bool isDirectory() const { return ty==std::filesystem::file_type::directory; }

private:
    //Fields that are written to diff files
    std::filesystem::file_type ty; ///< File type (regular, directory, ...)
    std::filesystem::perms per;    ///< File permissions (rwxrwxrwx)
    std::string us;                ///< File user (owner)
    std::string gs;                ///< File group
    time_t mt=0;                   ///< Modified time
    off_t sz=0;                    ///< Size, only if regular file
    std::string fileHash;          ///< SHA1 hash, only if regular file
    std::filesystem::path rp;      ///< File path relative to top level directory
    std::filesystem::path symlink; ///< Symlink target path, only if symlink

    //Fields that are not written to diff files
    uintmax_t hardLinkCnt=1;       ///< Number of hardlinks

    friend bool operator== (const FilesystemElement& a, const FilesystemElement& b);
};

/**
 * Compare operator for sorting
 */
bool operator< (const FilesystemElement& a, const FilesystemElement& b);

/**
 * Equality/inequality comparison
 */
bool operator== (const FilesystemElement& a, const FilesystemElement& b);
inline bool operator!= (const FilesystemElement& a, const FilesystemElement& b)
{
    return !(a==b);
}

/**
 * A node of an in-memory representation of the metadata of a directory tree
 */
class DirectoryNode
{
public:
    DirectoryNode() {}

    // Heavy object not ment to be copyable, only move assignable.
    // This declaration implicitly deletes copy constructor and copy assignment
    // in a way that does not upset std::list
    DirectoryNode(DirectoryNode&&)=default;

    /**
     * Constructor
     */
    explicit DirectoryNode(FilesystemElement elem) : elem(elem) {}

    /**
     * If the node is a directory, this member function allows to set its content
     * \param content, a list of other DirectoryNodes
     */
    std::list<DirectoryNode>& setDirectoryContent(std::list<DirectoryNode>&& content);

    /**
     * \return a const reference to the FilesystemElement of this node
     */
    const FilesystemElement& getElement() const { return elem; }

    /**
     * \return a const reference to the nodes contained in the directory, or an
     * empty list otherwise
     */
    const std::list<DirectoryNode>& getDirectoryContent() const { return content; }

private:
    FilesystemElement elem;
    std::list<DirectoryNode> content;
};

/**
 * Compare operator for sorting
 */
inline bool operator< (const DirectoryNode& a, const DirectoryNode& b)
{
    return a.getElement() < b.getElement();
}

/**
 * An in-memory representation of the metadata of a directory tree
 */
class DirectoryTree
{
public:
    DirectoryTree() {}

    /**
     * Construct a directory tree starting from either a diff file or a directory
     * \param inputPath if the path is to a directory, use it as the top level
     * directory where to start the directory tree, if it is a file path, assume
     * it is a path to a diff file
     */
    DirectoryTree(const std::filesystem::path& inputPath)
    {
        if(is_directory(inputPath)) scanDirectory(inputPath);
        else readFrom(inputPath);
    }
    
    /**
     * Construct a directory tree by reading from diff files
     * \param is istream where to read
     * \throws runtime_error in case of errors
     */
    DirectoryTree(std::istream& is, const std::string& diffFileName="")
    {
        readFrom(is,diffFileName);
    }

    /**
     * Scan directory tree starting from the given top path
     * \param topPath top level directory where to start the directory tree
     */
    void scanDirectory(const std::filesystem::path& topPath);

    /**
     * Read from diff files
     * \param diffFile path of the diff file
     */
    void readFrom(const std::filesystem::path& diffFile);

    /**
     * Read from diff files
     * \param is istream where to read
     * \throws runtime_error in case of errors
     */
    void readFrom(std::istream& is, const std::string& diffFileName="");

    /**
     * Write the object to an ostream based on the diff file format
     * \param os ostream where to write
     */
    void writeTo(std::ostream& os);
    
    /**
     * Deallocate the entire directory tree
     */
    void clear();

    /**
     * \return true if the last listFiles call encountered unsupported file
     * types
     */
    bool unsupportedFilesFound() const { return unsupported; }

    /**
     * \return the root of the directory tree, containing the content of the top
     * directory and all its subfolders, if any
     * NOTE: since all ellements are stored by value thiis may be a very
     * heavy object, never copy this tree, only access it by reference
     */
    const std::list<DirectoryNode>& getTreeRoot() const { return topContent; }

    /**
     * \return the a flat index of all the files and directories in the
     * directory tree, that is the top directory and all its subdirectories
     * The key is the relative path, while the value is a pointer into the tree
     * data structure
     */
    const std::unordered_map<std::string,DirectoryNode*>& getIndex() const
    {
        return index;
    }

private:
    void recursiveBuildFromPath(const std::filesystem::path& p);

    void recursiveWrite(const std::list<DirectoryNode>& nodes);

    bool unsupported=false;
    std::list<DirectoryNode> topContent;
    std::unordered_map<std::string,DirectoryNode*> index;
    std::filesystem::path topPath; // Only used by recursiveBuildFromPath
    std::ostream *os=nullptr; //Only used by recursiveWrite
    bool printBreak; //Only used by recursiveWrite
};

/**
 * Type returned by compare2
 * First element of the pair is a FilesystemElement of the A tree
 * Second element of the pair is a FilesystemElement of the B tree
 */
template<unsigned N>
using DirectoryDiff=std::list<std::array<std::optional<FilesystemElement>,N>>;

/**
 * Two way diff between two directory trees
 */
DirectoryDiff<2> compare2(const DirectoryTree& a, const DirectoryTree& b);
