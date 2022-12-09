/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MYSQL_SECRET_STORE_CORE_PROGRAM_H_
#define MYSQL_SECRET_STORE_CORE_PROGRAM_H_

#include <memory>
#include <vector>

#include "mysql-secret-store/include/helper.h"

#include "mysql-secret-store/core/command.h"

namespace mysql {
namespace secret_store {
namespace core {

class Program {
 public:
  explicit Program(std::unique_ptr<common::Helper> helper);

  Program(const Program &) = delete;
  Program(Program &&) = delete;
  Program &operator=(const Program &) = delete;
  Program &operator=(Program &&) = delete;

  int run(int argc, char *argv[]);

 private:
  std::unique_ptr<common::Helper> m_helper;
  std::vector<std::unique_ptr<Command>> m_commands;
};

}  // namespace core
}  // namespace secret_store
}  // namespace mysql

#endif  // MYSQL_SECRET_STORE_CORE_PROGRAM_H_