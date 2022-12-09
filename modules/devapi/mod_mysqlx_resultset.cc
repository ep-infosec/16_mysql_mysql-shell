/*
 * Copyright (c) 2014, 2022, Oracle and/or its affiliates.
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

#include "modules/devapi/mod_mysqlx_resultset.h"

#include <memory>
#include <utility>

#include "modules/devapi/base_constants.h"
#include "modules/mysqlxtest_utils.h"
#include "mysqlshdk/include/scripting/common.h"
#include "mysqlshdk/include/scripting/obj_date.h"
#include "mysqlshdk/include/scripting/type_info/custom.h"
#include "mysqlshdk/include/scripting/type_info/generic.h"
#include "mysqlshdk/include/shellcore/base_shell.h"
#include "mysqlshdk/include/shellcore/utils_help.h"
#include "mysqlshdk/libs/db/charset.h"
#include "mysqlshdk/libs/db/row_copy.h"
#include "mysqlshdk/libs/db/session.h"
#include "mysqlshdk/libs/utils/strformat.h"
#include "mysqlshdk/libs/utils/utils_json.h"

using shcore::Value;
using std::placeholders::_1;

namespace mysqlsh {
namespace mysqlx {

// -----------------------------------------------------------------------

// Documentation of BaseResult class
REGISTER_HELP_CLASS(BaseResult, mysqlx);
REGISTER_HELP(
    BASERESULT_BRIEF,
    "Base class for the different types of results returned by the server.");

BaseResult::BaseResult(std::shared_ptr<mysqlshdk::db::mysqlx::Result> result)
    : _result(result) {
  add_property("affectedItemsCount", "getAffectedItemsCount");
  add_property("executionTime", "getExecutionTime");
  add_property("warningCount", "getWarningCount");
  add_property("warningsCount", "getWarningsCount");
  add_property("warnings", "getWarnings");
}

BaseResult::~BaseResult() {}

// Documentation of getWarnings function
REGISTER_HELP_PROPERTY(warnings, BaseResult);
REGISTER_HELP(BASERESULT_WARNINGS_BRIEF, "Same as <<<getWarnings>>>");
REGISTER_HELP_FUNCTION(getWarnings, BaseResult);
REGISTER_HELP_FUNCTION_TEXT(BASERESULT_GETWARNINGS, R"*(
Retrieves the warnings generated by the executed operation.

@returns A list containing a warning object for each generated warning.

This is the same value than C API mysql_warning_count, see
https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

Each warning object contains a key/value pair describing the information
related to a specific warning.

This information includes: Level, Code and Message.
)*");
/**
 * $(BASERESULT_GETWARNINGS_BRIEF)
 *
 * $(BASERESULT_GETWARNINGS)
 */
#if DOXYGEN_JS
List BaseResult::getWarnings() {}
#elif DOXYGEN_PY
list BaseResult::get_warnings() {}
#endif

shcore::Value BaseResult::get_member(const std::string &prop) const {
  shcore::Value ret_val;

  if (prop == "affectedItemsCount") {
    ret_val = Value(get_affected_items_count());
  } else if (prop == "executionTime") {
    return shcore::Value(get_execution_time());
  } else if (prop == "warningCount") {
    log_warning("'%s' is deprecated, use '%s' instead.",
                get_function_name("warningCount").c_str(),
                get_function_name("warningsCount").c_str());
    ret_val = Value(get_warnings_count());
  } else if (prop == "warningsCount") {
    ret_val = Value(get_warnings_count());
  } else if (prop == "warnings") {
    std::shared_ptr<shcore::Value::Array_type> array(
        new shcore::Value::Array_type);
    if (_result) {
      while (std::unique_ptr<mysqlshdk::db::Warning> warning =
                 _result->fetch_one_warning()) {
        auto warning_row = std::make_shared<mysqlsh::Row>();
        switch (warning->level) {
          case mysqlshdk::db::Warning::Level::Note:
            warning_row->add_item("level", shcore::Value("Note"));
            break;
          case mysqlshdk::db::Warning::Level::Warn:
            warning_row->add_item("level", shcore::Value("Warning"));
            break;
          case mysqlshdk::db::Warning::Level::Error:
            warning_row->add_item("level", shcore::Value("Error"));
            break;
        }
        warning_row->add_item("code", shcore::Value(warning->code));
        warning_row->add_item("message", shcore::Value(warning->msg));

        array->push_back(shcore::Value::wrap(std::move(warning_row)));
      }
    }
    ret_val = shcore::Value(array);
  } else {
    ret_val = ShellBaseResult::get_member(prop);
  }

  return ret_val;
}

bool BaseResult::has_data() const {
  return _result && _result->has_resultset();
}

// Documentation of getAffectedItemCount function
REGISTER_HELP_PROPERTY(affectedItemsCount, BaseResult);
REGISTER_HELP(BASERESULT_AFFECTEDITEMSCOUNT_BRIEF,
              "Same as <<<getAffectedItemsCount>>>");

REGISTER_HELP_FUNCTION(getAffectedItemsCount, BaseResult);
REGISTER_HELP_FUNCTION_TEXT(BASERESULT_GETAFFECTEDITEMSCOUNT, R"*(
The the number of affected items for the last operation.

@returns the number of affected items.

Returns the number of records affected by the executed operation.
)*");
/**
 * $(BASERESULT_GETAFFECTEDITEMSCOUNT_BRIEF)
 *
 * $(BASERESULT_GETAFFECTEDITEMSCOUNT)
 */
#if DOXYGEN_JS
Integer BaseResult::getAffectedItemsCount() {}
#elif DOXYGEN_PY
int BaseResult::get_affected_items_count() {}
#endif

int64_t BaseResult::get_affected_items_count() const {
  if (!_result) return -1;
  return _result->get_affected_row_count();
}

// Documentation of getExecutionTime function
REGISTER_HELP_PROPERTY(executionTime, BaseResult);
REGISTER_HELP(BASERESULT_EXECUTIONTIME_BRIEF, "Same as <<<getExecutionTime>>>");
REGISTER_HELP_FUNCTION(getExecutionTime, BaseResult);
REGISTER_HELP(BASERESULT_GETEXECUTIONTIME_BRIEF,
              "Retrieves a string value indicating the execution time of the "
              "executed operation.");

/**
 * $(BASERESULT_GETEXECUTIONTIME_BRIEF)
 */
#if DOXYGEN_JS
String BaseResult::getExecutionTime() {}
#elif DOXYGEN_PY
str BaseResult::get_execution_time() {}
#endif

std::string BaseResult::get_execution_time() const {
  return mysqlshdk::utils::format_seconds(_result->get_execution_time());
}

// Documentation of getWarningCount function
REGISTER_HELP_PROPERTY(warningCount, BaseResult);
REGISTER_HELP(BASERESULT_WARNINGCOUNT_BRIEF, "Same as <<<getWarningCount>>>");
REGISTER_HELP(BASERESULT_WARNINGCOUNT_DETAIL,
              "${BASERESULT_WARNINGCOUNT_DEPRECATED}");
REGISTER_HELP(BASERESULT_WARNINGCOUNT_DEPRECATED,
              "@attention This property will be removed in a future release, "
              "use the <b><<<warningsCount>>></b> property instead.");

REGISTER_HELP_FUNCTION(getWarningCount, BaseResult);
REGISTER_HELP_FUNCTION_TEXT(BASERESULT_GETWARNINGCOUNT, R"*(
The number of warnings produced by the last statement execution.

@returns the number of warnings.

@attention This function will be removed in a future release, use the
<b><<<getWarningsCount>>></b> function instead.

This is the same value than C API mysql_warning_count, see
https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

See <<<getWarnings>>>() for more details.
)*");
/**
 * $(BASERESULT_GETWARNINGCOUNT_BRIEF)
 *
 * $(BASERESULT_GETWARNINGCOUNT)
 *
 * \sa warnings
 */
#if DOXYGEN_JS
Integer BaseResult::getWarningCount() {}
#elif DOXYGEN_PY
int BaseResult::get_warning_count() {}
#endif

// Documentation of getWarningCount function
REGISTER_HELP_PROPERTY(warningsCount, BaseResult);
REGISTER_HELP(BASERESULT_WARNINGSCOUNT_BRIEF, "Same as <<<getWarningsCount>>>");

REGISTER_HELP_FUNCTION(getWarningsCount, BaseResult);
REGISTER_HELP_FUNCTION_TEXT(BASERESULT_GETWARNINGSCOUNT, R"*(
The number of warnings produced by the last statement execution.

@returns the number of warnings.

This is the same value than C API mysql_warning_count, see
https://dev.mysql.com/doc/refman/en/mysql-warning-count.html

See <<<getWarnings>>>() for more details.
)*");
/**
 * $(BASERESULT_GETWARNINGSCOUNT_BRIEF)
 *
 * $(BASERESULT_GETWARNINGSCOUNT)
 *
 * \sa warnings
 */
#if DOXYGEN_JS
Integer BaseResult::getWarningsCount() {}
#elif DOXYGEN_PY
int BaseResult::get_warnings_count() {}
#endif

uint64_t BaseResult::get_warnings_count() const {
  if (_result) return _result->get_warning_count();
  return 0;
}

void BaseResult::append_json(shcore::JSON_dumper &dumper) const {
  bool create_object = (dumper.deep_level() == 0);

  if (create_object) dumper.start_object();

  dumper.append_value("executionTime", get_member("executionTime"));
  dumper.append_value("affectedItemsCount", get_member("affectedItemsCount"));

  if (mysqlsh::current_shell_options()->get().show_warnings) {
    dumper.append_value("warningCount", get_member("warningsCount"));
    dumper.append_value("warningsCount", get_member("warningsCount"));
    dumper.append_value("warnings", get_member("warnings"));
  }

  if (create_object) dumper.end_object();
}

// -----------------------------------------------------------------------

// Documentation of Result class
REGISTER_HELP_SUB_CLASS(Result, mysqlx, BaseResult);
REGISTER_HELP_CLASS_TEXT(RESULT, R"*(
Allows retrieving information about non query operations performed on the
database.

An instance of this class will be returned on the CRUD operations that change
the content of the database:

@li On Table: insert, update and delete
@li On Collection: add, modify and remove

Other functions on the Session class also return an instance of this class:

@li Transaction handling functions
)*");
Result::Result(std::shared_ptr<mysqlshdk::db::mysqlx::Result> result)
    : BaseResult(result) {
  add_property("affectedItemCount", "getAffectedItemCount");
  add_property("autoIncrementValue", "getAutoIncrementValue");
  add_property("generatedIds", "getGeneratedIds");
}

shcore::Value Result::get_member(const std::string &prop) const {
  Value ret_val;

  if (prop == "affectedItemCount") {
    ret_val = Value(get_affected_items_count());
    log_warning("'%s' is deprecated, use '%s' instead.",
                get_function_name("affectedItemCount").c_str(),
                get_function_name("affectedItemsCount").c_str());
  } else if (prop == "autoIncrementValue") {
    ret_val = Value(get_auto_increment_value());
  } else if (prop == "generatedIds") {
    auto array = shcore::make_array();

    for (auto &doc_id : get_generated_ids()) {
      array->emplace_back(std::move(doc_id));
    }

    ret_val = Value(std::move(array));
  } else {
    ret_val = BaseResult::get_member(prop);
  }

  return ret_val;
}

// Documentation of getAffectedItemCount function
REGISTER_HELP_PROPERTY(affectedItemCount, Result);
REGISTER_HELP(RESULT_AFFECTEDITEMCOUNT_BRIEF,
              "Same as <<<getAffectedItemCount>>>");
REGISTER_HELP(RESULT_AFFECTEDITEMCOUNT_DETAIL,
              "${RESULT_AFFECTEDITEMCOUNT_DEPRECATED}");
REGISTER_HELP(RESULT_AFFECTEDITEMCOUNT_DEPRECATED,
              "@attention This property will be removed in a future release, "
              "use the <b><<<affectedItemsCount>>></b> property instead.");

REGISTER_HELP_FUNCTION(getAffectedItemCount, Result);
REGISTER_HELP_FUNCTION_TEXT(RESULT_GETAFFECTEDITEMCOUNT, R"*(
The the number of affected items for the last operation.

@returns the number of affected items.

@attention This function will be removed in a future release, use the
<b><<<getAffectedItemsCount>>></b> function instead.

This is the value of the C API mysql_affected_rows(), see
https://dev.mysql.com/doc/refman/en/mysql-affected-rows.html
)*");
/**
 * $(RESULT_GETAFFECTEDITEMCOUNT_BRIEF)
 *
 * $(RESULT_GETAFFECTEDITEMCOUNT)
 */
#if DOXYGEN_JS
Integer Result::getAffectedItemCount() {}
#elif DOXYGEN_PY
int Result::get_affected_item_count() {}
#endif

// Documentation of getAutoIncrementValue function
REGISTER_HELP_PROPERTY(autoIncrementValue, Result);
REGISTER_HELP(RESULT_AUTOINCREMENTVALUE_BRIEF,
              "Same as <<<getAutoIncrementValue>>>");
REGISTER_HELP_FUNCTION(getAutoIncrementValue, Result);
REGISTER_HELP_FUNCTION_TEXT(RESULT_GETAUTOINCREMENTVALUE, R"*(
The last insert id auto generated (from an insert operation)

@returns the integer representing the last insert id

For more details, see 
https://dev.mysql.com/doc/refman/en/information-functions.html#function_last-insert-id

Note that this value will be available only when the result is for a
Table.insert operation.
)*");
/**
 * $(RESULT_GETAUTOINCREMENTVALUE_BRIEF)
 *
 * $(RESULT_GETAUTOINCREMENTVALUE)
 */
#if DOXYGEN_JS
Integer Result::getAutoIncrementValue() {}
#elif DOXYGEN_PY
int Result::get_auto_increment_value() {}
#endif

int64_t Result::get_auto_increment_value() const {
  if (_result) return _result->get_auto_increment_value();
  return 0;
}

// Documentation of getLastDocumentId function
REGISTER_HELP_PROPERTY(generatedIds, Result);
REGISTER_HELP(RESULT_GENERATEDIDS_BRIEF, "Same as <<<getGeneratedIds>>>.");
REGISTER_HELP_FUNCTION(getGeneratedIds, Result);
REGISTER_HELP_FUNCTION_TEXT(RESULT_GETGENERATEDIDS, R"*(
Returns the list of document ids generated on the server.

@returns a list of strings containing the generated ids.

When adding documents into a collection, it is required that an ID is
associated to the document, if a document is added without an '_id' field, an
error will be generated.

At MySQL 8.0.11 if the documents being added do not have an '_id' field, the
server will automatically generate an ID and assign it to the document.

This function returns a list of the IDs that were generated for the server to
satisfy this requirement.
)*");
/**
 * $(RESULT_GETGENERATEDIDS_BRIEF)
 *
 * $(RESULT_GETGENERATEDIDS)
 */
#if DOXYGEN_JS
List Result::getGeneratedIds() {}
#elif DOXYGEN_PY
list Result::get_generated_ids() {}
#endif
const std::vector<std::string> Result::get_generated_ids() const {
  if (_result)
    return _result->get_generated_ids();
  else
    return {};
}

void Result::append_json(shcore::JSON_dumper &dumper) const {
  dumper.start_object();

  BaseResult::append_json(dumper);

  dumper.append_value("affectedItemCount", get_member("affectedItemsCount"));
  dumper.append_value("autoIncrementValue", get_member("autoIncrementValue"));
  dumper.append_value("generatedIds", get_member("generatedIds"));

  dumper.end_object();
}

// -----------------------------------------------------------------------

// Documentation of DocResult class
REGISTER_HELP_SUB_CLASS(DocResult, mysqlx, BaseResult);
REGISTER_HELP(DOCRESULT_BRIEF,
              "Allows traversing the DbDoc objects returned by a "
              "Collection.find operation.");

DocResult::DocResult(std::shared_ptr<mysqlshdk::db::mysqlx::Result> result)
    : BaseResult(result) {
  expose("fetchOne", &DocResult::fetch_one);
  expose("fetchAll", &DocResult::fetch_all);
}

// Documentation of fetchOne function
REGISTER_HELP_FUNCTION(fetchOne, DocResult);
REGISTER_HELP_FUNCTION_TEXT(DOCRESULT_FETCHONE, R"*(
Retrieves the next DbDoc on the DocResult.

@returns A DbDoc object representing the next Document in the result.
)*");
/**
 * $(DOCRESULT_FETCHONE_BRIEF)
 *
 * $(DOCRESULT_FETCHONE)
 */
#if DOXYGEN_JS
Document DocResult::fetchOne() {}
#elif DOXYGEN_PY
Document DocResult::fetch_one() {}
#endif
shcore::Dictionary_t DocResult::fetch_one() const {
  shcore::Dictionary_t ret_val;

  if (_result) {
    if (const mysqlshdk::db::IRow *r = _result->fetch_one()) {
      ret_val = Value::parse(r->get_string(0)).as_map();
    }
  }

  return ret_val;
}

// Documentation of fetchAll function
REGISTER_HELP_FUNCTION(fetchAll, DocResult);
REGISTER_HELP_FUNCTION_TEXT(DOCRESULT_FETCHALL, R"*(
Returns a list of DbDoc objects which contains an element for every unread
document.

@returns A List of DbDoc objects.

If this function is called right after executing a query, it will return a
DbDoc for every document on the resultset.

If fetchOne is called before this function, when this function is called it
will return a DbDoc for each of the remaining documents on the resultset.
)*");
/**
 * $(DOCRESULT_FETCHALL_BRIEF)
 *
 * $(DOCRESULT_FETCHALL)
 */
#if DOXYGEN_JS
List DocResult::fetchAll() {}
#elif DOXYGEN_PY
list DocResult::fetch_all() {}
#endif
shcore::Array_t DocResult::fetch_all() const {
  auto array = shcore::make_array();

  // Gets the next document
  auto record = fetch_one();
  while (record) {
    array->push_back(shcore::Value(record));
    record = fetch_one();
  }

  return array;
}

void DocResult::append_json(shcore::JSON_dumper &dumper) const {
  dumper.start_object();

  dumper.append_value("documents", shcore::Value(fetch_all()));

  BaseResult::append_json(dumper);

  dumper.end_object();
}

// -----------------------------------------------------------------------

// Documentation of RowResult class
REGISTER_HELP_SUB_CLASS(RowResult, mysqlx, BaseResult);
REGISTER_HELP(
    ROWRESULT_BRIEF,
    "Allows traversing the Row objects returned by a Table.select operation.");

RowResult::RowResult(std::shared_ptr<mysqlshdk::db::mysqlx::Result> result)
    : BaseResult(result) {
  add_property("columnCount", "getColumnCount");
  add_property("columns", "getColumns");
  add_property("columnNames", "getColumnNames");

  expose("fetchOne", &RowResult::fetch_one);
  expose("fetchAll", &RowResult::fetch_all);
  expose("fetchOneObject", &RowResult::_fetch_one_object);
}

shcore::Value RowResult::get_member(const std::string &prop) const {
  Value ret_val;
  if (prop == "columnCount") {
    ret_val = shcore::Value(get_column_count());
  } else if (prop == "columnNames") {
    update_column_cache();
    auto array = shcore::make_array();

    if (m_column_names) {
      for (auto &column : *m_column_names) {
        array->push_back(shcore::Value(column));
      }
    }

    ret_val = shcore::Value(array);
  } else if (prop == "columns") {
    update_column_cache();

    if (m_columns) {
      ret_val = shcore::Value(m_columns);
    } else {
      ret_val = shcore::Value(shcore::make_array());
    }
  } else {
    ret_val = BaseResult::get_member(prop);
  }
  return ret_val;
}

// Documentation of getColumnCount function
REGISTER_HELP_PROPERTY(columnCount, RowResult);
REGISTER_HELP(ROWRESULT_COLUMNCOUNT_BRIEF, "Same as <<<getColumnCount>>>");
REGISTER_HELP_FUNCTION(getColumnCount, RowResult);
REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_GETCOLUMNCOUNT, R"*(
Retrieves the number of columns on the current result.

@returns the number of columns on the current result.
)*");
/**
 * $(ROWRESULT_GETCOLUMNCOUNT_BRIEF)
 *
 * $(ROWRESULT_GETCOLUMNCOUNT)
 */
#if DOXYGEN_JS
Integer RowResult::getColumnCount() {}
#elif DOXYGEN_PY
int RowResult::get_column_count() {}
#endif
int64_t RowResult::get_column_count() const {
  return _result->get_metadata().size();
}

// Documentation of getColumnNames function
REGISTER_HELP_PROPERTY(columnNames, RowResult);
REGISTER_HELP(ROWRESULT_COLUMNNAMES_BRIEF, "Same as <<<getColumnNames>>>");
REGISTER_HELP_FUNCTION(getColumnNames, RowResult);
REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_GETCOLUMNNAMES, R"*(
Gets the columns on the current result.

@returns A list with the names of the columns returned on the active result.
)*");
/**
 * $(ROWRESULT_GETCOLUMNNAMES_BRIEF)
 *
 * $(ROWRESULT_GETCOLUMNNAMES)
 */
#if DOXYGEN_JS
List RowResult::getColumnNames() {}
#elif DOXYGEN_PY
list RowResult::get_column_names() {}
#endif

// Documentation of getColumns function
REGISTER_HELP_PROPERTY(columns, RowResult);
REGISTER_HELP(ROWRESULT_COLUMNS_BRIEF, "Same as <<<getColumns>>>");
REGISTER_HELP_FUNCTION(getColumns, RowResult);
REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_GETCOLUMNS, R"*(
Gets the column metadata for the columns on the active result.

@returns a list of Column objects containing information about the columns
included on the active result.
)*");
/**
 * $(ROWRESULT_GETCOLUMNS_BRIEF)
 *
 * $(ROWRESULT_GETCOLUMNS)
 */
#if DOXYGEN_JS
List RowResult::getColumns() {}
#elif DOXYGEN_PY
list RowResult::get_columns() {}
#endif

// Documentation of fetchOne function
REGISTER_HELP_FUNCTION(fetchOne, RowResult);
REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_FETCHONE, R"*(
Retrieves the next Row on the RowResult.

@returns A Row object representing the next record on the result.
)*");
/**
 * $(ROWRESULT_FETCHONE_BRIEF)
 *
 * $(ROWRESULT_FETCHONE)
 */
#if DOXYGEN_JS
Row RowResult::fetchOne() {}
#elif DOXYGEN_PY
Row RowResult::fetch_one() {}
#endif
std::shared_ptr<mysqlsh::Row> RowResult::fetch_one() const {
  std::shared_ptr<mysqlsh::Row> ret_val;

  auto row = fetch_one_row();
  if (row) {
    ret_val = std::shared_ptr<mysqlsh::Row>(row.release());
  }

  return ret_val;
}

REGISTER_HELP_FUNCTION(fetchOneObject, RowResult);

REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_FETCHONEOBJECT, R"*(
Retrieves the next Row on the result and returns it as an object.

@returns A dictionary containing the row information.

The column names will be used as keys in the returned dictionary and the column
data will be used as the key values.

If a column is a valid identifier it will be accessible as an object attribute
as @<dict@>.@<column@>.

If a column is not a valid identifier, it will be accessible as a dictionary
key as @<dict@>[@<column@>].
)*");
/**
 * $(ROWRESULT_FETCHONEOBJECT_BRIEF)
 *
 * $(ROWRESULT_FETCHONEOBJECT)
 */
#if DOXYGEN_JS
Dictionary RowResult::fetchOneObject() {}
#elif DOXYGEN_PY
dict RowResult::fetch_one_object() {}
#endif
shcore::Dictionary_t RowResult::_fetch_one_object() {
  return ShellBaseResult::fetch_one_object();
}

// Documentation of fetchAll function
REGISTER_HELP_FUNCTION(fetchAll, RowResult);
REGISTER_HELP_FUNCTION_TEXT(ROWRESULT_FETCHALL, R"*(
Returns a list of DbDoc objects which contains an element for every unread
document.

@returns A List of DbDoc objects.
)*");
/**
 * $(ROWRESULT_FETCHALL_BRIEF)
 *
 * $(ROWRESULT_FETCHALL)
 */
#if DOXYGEN_JS
List RowResult::fetchAll() {}
#elif DOXYGEN_PY
list RowResult::fetch_all() {}
#endif
shcore::Array_t RowResult::fetch_all() const {
  auto array = shcore::make_array();

  // Gets the next row
  auto record = fetch_one();
  while (record) {
    array->push_back(shcore::Value(record));
    record = fetch_one();
  }

  return array;
}

void RowResult::append_json(shcore::JSON_dumper &dumper) const {
  bool create_object = (dumper.deep_level() == 0);

  if (create_object) dumper.start_object();

  BaseResult::append_json(dumper);

  dumper.append_value("rows", shcore::Value(fetch_all()));

  if (create_object) dumper.end_object();
}

// Documentation of SqlResult class
REGISTER_HELP_SUB_CLASS(SqlResult, mysqlx, RowResult);
REGISTER_HELP(SQLRESULT_BRIEF,
              "Allows browsing through the result information after performing "
              "an operation on the database done through Session.sql");

SqlResult::SqlResult(std::shared_ptr<mysqlshdk::db::mysqlx::Result> result)
    : RowResult(result) {
  expose("hasData", &SqlResult::has_data);
  expose("nextDataSet", &SqlResult::next_data_set);
  expose("nextResult", &SqlResult::next_result);
  add_property("autoIncrementValue", "getAutoIncrementValue");
  add_property("affectedRowCount", "getAffectedRowCount");
}

// Documentation of getAutoIncrementValue function
REGISTER_HELP_PROPERTY(autoIncrementValue, SqlResult);
REGISTER_HELP(SQLRESULT_AUTOINCREMENTVALUE_BRIEF,
              "Same as <<<getAutoIncrementValue>>>");
REGISTER_HELP_FUNCTION(getAutoIncrementValue, SqlResult);
REGISTER_HELP_FUNCTION_TEXT(SQLRESULT_GETAUTOINCREMENTVALUE, R"*(
Returns the identifier for the last record inserted.

Note that this value will only be set if the executed statement inserted a
record in the database and an ID was automatically generated.
)*");
/**
 * $(SQLRESULT_GETAUTOINCREMENTVALUE_BRIEF)
 *
 * $(SQLRESULT_GETAUTOINCREMENTVALUE)
 */
#if DOXYGEN_JS
Integer SqlResult::getAutoIncrementValue() {}
#elif DOXYGEN_PY
int SqlResult::get_auto_increment_value() {}
#endif
int64_t SqlResult::get_auto_increment_value() const {
  if (_result) return _result->get_auto_increment_value();
  return 0;
}

// Documentation of getAffectedRowCount function
REGISTER_HELP_PROPERTY(affectedRowCount, SqlResult);
REGISTER_HELP(SQLRESULT_AFFECTEDROWCOUNT_BRIEF,
              "Same as <<<getAffectedRowCount>>>");
REGISTER_HELP(SQLRESULT_AFFECTEDROWCOUNT_DETAIL,
              "${SQLRESULT_AFFECTEDROWCOUNT_DEPRECATED}");
REGISTER_HELP(SQLRESULT_AFFECTEDROWCOUNT_DEPRECATED,
              "@attention This property will be removed in a future release, "
              "use the <b><<<affectedItemsCount>>></b> property instead.");
REGISTER_HELP_FUNCTION(getAffectedRowCount, SqlResult);
REGISTER_HELP_FUNCTION_TEXT(SQLRESULT_GETAFFECTEDROWCOUNT, R"*(
Returns the number of rows affected by the executed query.

@attention This function will be removed in a future release, use the
<b><<<getAffectedItemsCount>>></b> function instead.
)*");
/**
 * $(SQLRESULT_GETAFFECTEDROWCOUNT_BRIEF)
 *
 * $(SQLRESULT_GETAFFECTEDROWCOUNT)
 */
#if DOXYGEN_JS
Integer SqlResult::getAffectedRowCount() {}
#elif DOXYGEN_PY
int SqlResult::get_affected_row_count() {}
#endif
int64_t SqlResult::get_affected_row_count() const {
  if (_result) return _result->get_affected_row_count();
  return 0;
}

shcore::Value SqlResult::get_member(const std::string &prop) const {
  Value ret_val;
  if (prop == "autoIncrementValue") {
    ret_val = Value(get_auto_increment_value());
  } else if (prop == "affectedRowCount") {
    ret_val = Value(get_affected_row_count());
    log_warning("'%s' is deprecated, use '%s' instead.",
                get_function_name("affectedRowCount").c_str(),
                get_function_name("affectedItemsCount").c_str());
  } else {
    ret_val = RowResult::get_member(prop);
  }

  return ret_val;
}

// Documentation of hasData function
REGISTER_HELP_FUNCTION(hasData, SqlResult);
REGISTER_HELP(SQLRESULT_HASDATA_BRIEF,
              "Returns true if the last statement execution has a result set.");

/**
 * $(SQLRESULT_HASDATA_BRIEF)
 */
#if DOXYGEN_JS
Bool SqlResult::hasData() {}
#elif DOXYGEN_PY
bool SqlResult::has_data() {}
#endif

// Documentation of nextDataSet function
REGISTER_HELP_FUNCTION(nextDataSet, SqlResult);
REGISTER_HELP_FUNCTION_TEXT(SQLRESULT_NEXTDATASET, R"*(
Prepares the SqlResult to start reading data from the next Result (if many
results were returned).

@returns A boolean value indicating whether there is another result or not.

@attention This function will be removed in a future release, use the
<b><<<nextResult>>></b> function instead.
)*");
/**
 * $(SQLRESULT_NEXTDATASET_BRIEF)
 *
 * $(SQLRESULT_NEXTDATASET)
 */
#if DOXYGEN_JS
Bool SqlResult::nextDataSet() {}
#elif DOXYGEN_PY
bool SqlResult::next_data_set() {}
#endif
bool SqlResult::next_data_set() {
  log_warning("'%s' is deprecated, use '%s' instead.",
              get_function_name("nextDataSet").c_str(),
              get_function_name("nextResult").c_str());

  return next_result();
}

// Documentation of nextDataSet function
REGISTER_HELP_FUNCTION(nextResult, SqlResult);
REGISTER_HELP_FUNCTION_TEXT(SQLRESULT_NEXTRESULT, R"*(
Prepares the SqlResult to start reading data from the next Result (if many
results were returned).

@returns A boolean value indicating whether there is another result or not.
)*");
/**
 * $(SQLRESULT_NEXTRESULT_BRIEF)
 *
 * $(SQLRESULT_NEXTRESULT)
 */
#if DOXYGEN_JS
Bool SqlResult::nextResult() {}
#elif DOXYGEN_PY
bool SqlResult::next_result() {}
#endif
bool SqlResult::next_result() {
  reset_column_cache();
  return _result->next_resultset();
}

void SqlResult::append_json(shcore::JSON_dumper &dumper) const {
  dumper.start_object();

  RowResult::append_json(dumper);

  dumper.append_value("hasData", shcore::Value(has_data()));
  dumper.append_value("affectedRowCount", get_member("affectedItemsCount"));
  dumper.append_value("autoIncrementValue", get_member("autoIncrementValue"));

  dumper.end_object();
}

}  // namespace mysqlx
}  // namespace mysqlsh
