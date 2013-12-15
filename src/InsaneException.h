/*
 *  InsaneException.h
 *
 *  This file is part of insaned.
 *  insaned is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  insaned is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with insaned; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *  Copyright (C) 2007-2013 Alex Busenius <the_unknown@gmx.net>
 */

#ifndef INSANEEXCEPTION_H
#define INSANEEXCEPTION_H


#include <string>
#include <stdexcept>


/**
 * The InsaneException class
 */
class InsaneException : std::exception
{
public:
    InsaneException(std::string message)
        : std::exception(),
          mMessage(message) {
    }

    virtual ~InsaneException();

    const char* what() const throw() override {
        return mMessage.c_str();
    }

private:
    std::string mMessage;
};

#endif
