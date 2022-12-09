/*
 * Copyright (c) 2018, 2022, Oracle and/or its affiliates.
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

#include <map>

#include "modules/adminapi/common/common.h"
#include "modules/adminapi/common/preconditions.h"
#include "modules/adminapi/common/provision.h"
#include "modules/adminapi/common/sql.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "mysqlshdk/libs/config/config_file_handler.h"
#include "mysqlshdk/libs/config/config_server_handler.h"
#include "mysqlshdk/libs/mysql/async_replication.h"
#include "mysqlshdk/libs/mysql/group_replication.h"
#include "mysqlshdk/libs/mysql/repl_config.h"
#include "mysqlshdk/libs/mysql/replication.h"
#include "mysqlshdk/libs/mysql/utils.h"
#include "mysqlshdk/libs/utils/nullable.h"
#include "mysqlshdk/libs/utils/utils_general.h"

namespace {

void set_gr_options(const mysqlshdk::mysql::IInstance &instance,
                    const mysqlsh::dba::Group_replication_options &gr_opts,
                    mysqlshdk::config::Config *config,
                    const mysqlshdk::utils::nullable<bool> &single_primary_mode,
                    const std::string &gr_group_name = "",
                    const std::string &gr_view_change_uuid = "") {
  // An non-null Config is expected.
  assert(config);

  // Set GR group name (determined by the caller).
  if (!gr_group_name.empty()) {
    // Obtained from the peer instance for join_cluster().
    config->set("group_replication_group_name",
                mysqlshdk::utils::nullable<std::string>(gr_group_name));
  } else if (!gr_opts.group_name.is_null()) {
    // Set by user or automatically generated by caller of start_cluster().
    config->set("group_replication_group_name", gr_opts.group_name,
                "groupName");
  }

  // Set GR view change UUID (determined by the caller).
  if (!gr_view_change_uuid.empty()) {
    // Obtained from the peer instance for join_cluster().
    config->set("group_replication_view_change_uuid",
                mysqlshdk::utils::nullable<std::string>(gr_view_change_uuid));
  } else if (!gr_opts.view_change_uuid.is_null()) {
    // Set by user or automatically generated by caller of start_cluster().
    config->set("group_replication_view_change_uuid", gr_opts.view_change_uuid,
                "viewChangeUUID");
  }

  // Set the GR primary mode (topology mode) (if provided).
  if (!single_primary_mode.is_null()) {
    // Enable GR enforce update everywhere checks for multi-primary clusters
    // and disable it for single-primary.
    // Note: order matters, enforce_update must be already OFF if we're enabling
    // single_primary_mode
    if (*single_primary_mode) {
      config->set("group_replication_enforce_update_everywhere_checks",
                  mysqlshdk::utils::nullable<bool>(false));

      config->set("group_replication_single_primary_mode",
                  mysqlshdk::utils::nullable<bool>(true));
    } else {
      config->set("group_replication_single_primary_mode",
                  mysqlshdk::utils::nullable<bool>(false));

      config->set("group_replication_enforce_update_everywhere_checks",
                  mysqlshdk::utils::nullable<bool>(true));
    }
  }

  // Handle SSL-mode
  {
    // SET the GR SSL variable according to the ssl_mode value (resolved by the
    // caller).
    if (gr_opts.ssl_mode == mysqlsh::dba::Cluster_ssl_mode::DISABLED) {
      if (instance.get_version() >= mysqlshdk::utils::Version(8, 0, 5)) {
        // This option is required to connect using the new
        // caching_sha256_password authentication method without SSL.
        log_debug("Enable 'group_replication_recovery_get_public_key'.");
        config->set("group_replication_recovery_get_public_key",
                    mysqlshdk::utils::nullable<bool>(true));
      }

      // Disable SSL on GR
      config->set("group_replication_recovery_use_ssl",
                  mysqlshdk::utils::nullable<bool>(false));
    } else {
      // Enable SSL on GR
      config->set("group_replication_recovery_use_ssl",
                  mysqlshdk::utils::nullable<bool>(true));

      // Set GR's SSL configurations when memberSslMode is either VERIFY_CA or
      // VERIFY_IDENTITY. Group Replication SSL settings must be set according
      // to the corresponding settings used by the Server (copy the values),
      // regardless of the communication stack in use

      if (gr_opts.ssl_mode == mysqlsh::dba::Cluster_ssl_mode::VERIFY_CA ||
          gr_opts.ssl_mode == mysqlsh::dba::Cluster_ssl_mode::VERIFY_IDENTITY) {
        std::string ssl_ca = instance.get_sysvar_string("ssl_ca").get_safe("");
        std::string ssl_capath =
            instance.get_sysvar_string("ssl_capath").get_safe("");
        std::string ssl_cert =
            instance.get_sysvar_string("ssl_cert").get_safe("");
        std::string ssl_cipher =
            instance.get_sysvar_string("ssl_cipher").get_safe("");
        std::string ssl_crl =
            instance.get_sysvar_string("ssl_crl").get_safe("");
        std::string ssl_crl_path =
            instance.get_sysvar_string("ssl_crlpath").get_safe("");
        std::string ssl_crl_key =
            instance.get_sysvar_string("ssl_key").get_safe("");

        config->set("group_replication_recovery_ssl_ca", ssl_ca);
        config->set("group_replication_recovery_ssl_capath", ssl_capath);
        config->set("group_replication_recovery_ssl_cert", ssl_cert);
        config->set("group_replication_recovery_ssl_cipher", ssl_cipher);
        config->set("group_replication_recovery_ssl_crl", ssl_crl);
        config->set("group_replication_recovery_ssl_crlpath", ssl_crl_path);
        config->set("group_replication_recovery_ssl_key", ssl_crl_key);
      } else {
        // Reset to the defaults in case the options are already set or
        // persisted with different values
        instance.set_sysvar_default("group_replication_recovery_ssl_ca");
        instance.set_sysvar_default("group_replication_recovery_ssl_capath");
        instance.set_sysvar_default("group_replication_recovery_ssl_cert");
        instance.set_sysvar_default("group_replication_recovery_ssl_cipher");
        instance.set_sysvar_default("group_replication_recovery_ssl_crl");
        instance.set_sysvar_default("group_replication_recovery_ssl_crlpath");
        instance.set_sysvar_default("group_replication_recovery_ssl_key");
      }
    }

    // Set the ssl_mode
    config->set("group_replication_ssl_mode", to_string(gr_opts.ssl_mode),
                "memberSslMode");
  }

  // The local_address value is determined based on the given localAddress
  // option (resolved by the caller).
  config->set("group_replication_local_address", gr_opts.local_address,
              "localAddress");

  // group_seeds is set by the caller
  if (!gr_opts.group_seeds.is_null()) {
    config->set("group_replication_group_seeds", gr_opts.group_seeds);
  }

  // Set GR IP whitelist (if provided).
  if (!gr_opts.ip_allowlist.is_null()) {
    if (instance.get_version() < mysqlshdk::utils::Version(8, 0, 22)) {
      config->set("group_replication_ip_whitelist", gr_opts.ip_allowlist,
                  "ipWhitelist");
    } else {
      config->set("group_replication_ip_allowlist", gr_opts.ip_allowlist,
                  "ipAllowlist");
    }
  }

  // Set GR exit state action (if provided).
  if (!gr_opts.exit_state_action.is_null()) {
    // GR exit state action can be an index value, in this case convert it to
    // an integer otherwise an SQL error will occur when using this value
    // (because it will try to set an int as as string).
    mysqlshdk::config::set_indexable_option(
        config, "group_replication_exit_state_action",
        gr_opts.exit_state_action, "exitStateAction");
  }

  // Set GR member weight (if provided).
  if (!gr_opts.member_weight.is_null()) {
    config->set("group_replication_member_weight", gr_opts.member_weight,
                "memberWeight");
  }

  // Set GR (failover) consistency (if provided).
  if (!gr_opts.consistency.is_null()) {
    // GR consistency can be an index value, in this case convert it to
    // an integer otherwise an SQL error will occur when using this value
    // (because it will try to set an int as as string).
    mysqlshdk::config::set_indexable_option(config,
                                            "group_replication_consistency",
                                            gr_opts.consistency, "consistency");
  }

  // Set GR expel timeout (if provided).
  if (!gr_opts.expel_timeout.is_null()) {
    config->set("group_replication_member_expel_timeout", gr_opts.expel_timeout,
                "expelTimeout");
  }

  // Set GR auto-rejoin tries (if provided).
  if (!gr_opts.auto_rejoin_tries.is_null()) {
    config->set("group_replication_autorejoin_tries", gr_opts.auto_rejoin_tries,
                "autoRejoinTries");
  }

  // Enable GR start on boot (unless disabled).
  config->set("group_replication_start_on_boot",
              mysqlshdk::utils::nullable<bool>(
                  !gr_opts.manual_start_on_boot.get_safe(false)));

  // Set GR communication stack (if provided).
  if (!gr_opts.communication_stack.is_null()) {
    config->set("group_replication_communication_stack",
                gr_opts.communication_stack, "communicationStack");
  }

  // Set GR transaction size limit (if provided).
  if (!gr_opts.transaction_size_limit.is_null()) {
    config->set(mysqlsh::dba::kGrTransactionSizeLimit,
                gr_opts.transaction_size_limit, "transactionSizeLimit");
  }
}

void report_gr_start_error(const mysqlshdk::mysql::IInstance &instance,
                           const std::string &before_time) {
  auto console = mysqlsh::current_console();
  bool first = true;

  if (!mysqlshdk::mysql::query_server_errors(
          instance, before_time, "", {"Repl"},
          [&first, &instance,
           console](const mysqlshdk::mysql::Error_log_entry &entry) {
            if (first) {
              console->print_error(
                  "Unable to start Group Replication for instance '" +
                  instance.descr() + "'.");
              console->print_info(
                  "The MySQL error_log contains the following messages:");
              first = false;
            }
            console->print_info(shcore::str_format(
                "  %s [%s] [%s] %s", entry.logged.c_str(), entry.prio.c_str(),
                entry.error_code.c_str(), entry.data.c_str()));
          })) {
    console->print_error(
        "Unable to start Group Replication for instance '" + instance.descr() +
        "'. Please check the MySQL server error log for more information.");
  }
}

void start_group_replication(const mysqlshdk::mysql::IInstance &instance,
                             bool bootstrap) {
  // Start Group Replication (effectively start the cluster).
  // NOTE: Creating and setting the recovery user must be performed after
  //       bootstrapping the GR group for all transcations to use the group
  //       UUID (see: BUG#28064729).
  std::string before_time = instance.queryf_one_string(0, "", "SELECT NOW(6)");
  try {
    mysqlshdk::gr::start_group_replication(instance, bootstrap);
  } catch (const shcore::Error &err) {
    report_gr_start_error(instance, before_time);

    std::string error_msg = "Group Replication failed to start: ";
    error_msg.append(err.format());
    throw shcore::Exception::runtime_error(error_msg);
  }
}

const char *kErrorReadOnlyTimeout =
    "Timeout waiting for super_read_only to be "
    "unset after call to start Group "
    "Replication plugin.";

void wait_super_read_only_cleared(const mysqlshdk::mysql::IInstance &instance,
                                  uint64_t read_only_timeout = 900) {
  // Wait for SUPER READ ONLY to be OFF.
  // Required for MySQL versions < 5.7.20.
  mysqlshdk::utils::nullable<bool> read_only = instance.get_sysvar_bool(
      "super_read_only", mysqlshdk::mysql::Var_qualifier::GLOBAL);

  if (*read_only) {
    // Check if super_read_only management is disabled
    if (instance.get_version() >= mysqlshdk::utils::Version(8, 0, 26)) {
      bool sro_auto_clears;
      if (!mysqlshdk::gr::get_member_action_status(
              instance, mysqlshdk::gr::k_gr_disable_super_read_only_if_primary,
              &sro_auto_clears))
        sro_auto_clears = true;

      if (!sro_auto_clears) {
        log_debug(
            "Skipping super_read_only wait at %s because member action is "
            "disabled",
            instance.descr().c_str());
        return;
      }
    }

    log_debug("Waiting for super_read_only to get cleared at %s",
              instance.descr().c_str());
    uint16_t waiting_time = 0;
    while (*read_only && waiting_time < read_only_timeout) {
      shcore::sleep_ms(1000);
      waiting_time += 1;
      read_only = instance.get_sysvar_bool(
          "super_read_only", mysqlshdk::mysql::Var_qualifier::GLOBAL);
    }
    // Throw an error is SUPPER READ ONLY is ON.
    if (*read_only) throw std::runtime_error(kErrorReadOnlyTimeout);
  }
}

}  // namespace

namespace mysqlsh {
namespace dba {

void leave_cluster(const mysqlsh::dba::Instance &instance,
                   bool reset_member_actions, bool reset_repl_channels) {
  std::string instance_address = instance.get_connection_options().as_uri(
      mysqlshdk::db::uri::formats::only_transport());

  auto console = mysqlsh::current_console();

  // Check if the instance is actively member of the cluster before trying to
  // stop it (otherwise stop might fail).
  mysqlshdk::gr::Member_state state = mysqlshdk::gr::get_member_state(instance);
  if (state != mysqlshdk::gr::Member_state::OFFLINE &&
      state != mysqlshdk::gr::Member_state::MISSING) {
    // Stop Group Replication (metadata already removed)
    console->print_info("* Instance '" + instance_address +
                        "' is attempting to leave the cluster...");
    mysqlshdk::gr::stop_group_replication(instance);
    // Get final state and log info.
    state = mysqlshdk::gr::get_member_state(instance);
    log_debug("Instance state after stopping Group Replication: %s",
              mysqlshdk::gr::to_string(state).c_str());
  } else {
    console->print_note("The instance '" + instance_address + "' is " +
                        mysqlshdk::gr::to_string(state) +
                        ", Group Replication stop skipped.");
  }

  // Reset the replication channels used by Group Replication.
  if (reset_repl_channels) {
    std::string replica_term =
        mysqlshdk::mysql::get_replica_keyword(instance.get_version());

    instance.executef("RESET " + replica_term + " ALL FOR CHANNEL ?",
                      "group_replication_applier");
    instance.executef("RESET " + replica_term + " ALL FOR CHANNEL ?",
                      "group_replication_recovery");
  }

  // Disable and persist GR start on boot and reset values for
  // group_replication_bootstrap_group,
  // group_replication_group_seeds and group_replication_local_address
  // NOTE: Only for server supporting SET PERSIST, version must be >= 8.0.11
  // due to BUG#26495619.
  log_debug(
      "Disabling needed group replication variables after stopping Group "
      "Replication, using SET PERSIST (if supported)");
  if (instance.get_version() >= mysqlshdk::utils::Version(8, 0, 11)) {
    const char *k_gr_remove_instance_vars_default[]{
        "group_replication_bootstrap_group", "group_replication_group_seeds",
        "group_replication_local_address"};
    instance.set_sysvar("group_replication_start_on_boot", false,
                        mysqlshdk::mysql::Var_qualifier::PERSIST);

    // GR enforce update everywhere checks must be set to OFF to allow reusing
    // the instance (e.g., in another (single-primary) cluster).
    // NOTE: Cannot set this variable (fail) to the value DEFAULT.
    instance.set_sysvar("group_replication_enforce_update_everywhere_checks",
                        false, mysqlshdk::mysql::Var_qualifier::PERSIST);

    for (auto gr_var : k_gr_remove_instance_vars_default) {
      instance.set_sysvar_default(gr_var,
                                  mysqlshdk::mysql::Var_qualifier::PERSIST);
    }

    bool persist_load = *instance.get_sysvar_bool(
        "persisted_globals_load", mysqlshdk::mysql::Var_qualifier::GLOBAL);
    if (!persist_load) {
      std::string warn_msg =
          "On instance '" + instance_address +
          "' the persisted cluster configuration will not be loaded upon "
          "reboot since 'persisted-globals-load' is set to 'OFF'. Please set "
          "'persisted-globals-load' to 'ON' on the configuration file or set "
          "the 'group_replication_start_on_boot' variable to 'OFF' in the "
          "server configuration file, otherwise it might rejoin the cluster "
          "upon restart.";
      console->print_warning(warn_msg);
    }
  } else {
    std::string warn_msg =
        "On instance '" + instance_address +
        "' configuration cannot be persisted since MySQL version " +
        instance.get_version().get_base() +
        " does not support the SET PERSIST command (MySQL version >= 8.0.11 "
        "required). Please set the 'group_replication_start_on_boot' variable "
        "to 'OFF' in the server configuration file, otherwise it might rejoin "
        "the cluster upon restart.";
    console->print_warning(warn_msg);
  }

  // Reset any member action value to the default since might have been changed
  // if the instance belonged to a ClusterSet:
  //   - mysql_disable_super_read_only_if_primary
  //   - mysql_start_failover_channels_if_primary
  if (reset_member_actions) {
    try {
      mysqlshdk::gr::reset_member_actions(instance);
    } catch (...) {
      log_error("Error resetting Group Replication member actions at %s: %s",
                instance.descr().c_str(), format_active_exception().c_str());
      throw;
    }
  }
}

std::vector<mysqlshdk::mysql::Invalid_config> check_instance_config(
    const mysqlshdk::mysql::IInstance &instance,
    const mysqlshdk::config::Config &config, Cluster_type cluster_type) {
  auto invalid_cfgs_vec = std::vector<mysqlshdk::mysql::Invalid_config>();

  // validate server_id
  mysqlshdk::mysql::check_server_id_compatibility(instance, config,
                                                  &invalid_cfgs_vec);
  // validate log_bin
  mysqlshdk::mysql::check_log_bin_compatibility(instance, config,
                                                &invalid_cfgs_vec);
  // validate rest of server variables required for gr
  mysqlshdk::mysql::check_server_variables_compatibility(
      instance, config, cluster_type == Cluster_type::GROUP_REPLICATION,
      &invalid_cfgs_vec);

  // NOTE: The order in the invalid_cfgs_vec is important since this vector
  // will be used for the configure_instance operation to set the correct
  // values for the variables and there are dependencies between some variables,
  // i.e, some variables need to be set before others.

  // Check if the config server handler supports the set persist syntax.
  auto srv_cfg_handler =
      dynamic_cast<mysqlshdk::config::Config_server_handler *>(
          config.get_handler(mysqlshdk::config::k_dft_cfg_server_handler));
  bool cannot_persist = (srv_cfg_handler->get_default_var_qualifier() !=
                         mysqlshdk::mysql::Var_qualifier::PERSIST);

  // For each of the configs, if the user didn't provide a configuration
  // file and the instance doesn't support the set persist of the variable, then
  // we need to write that change to the configuration file.
  // NOTE: All the required variables (not only the read-only ones) need to be
  //       persisted in the option file in order to "survive" server restarts
  //       (see BUG#30171090).

  if (cannot_persist &&
      !config.has_handler(mysqlshdk::config::k_dft_cfg_file_handler)) {
    for (auto &invalid_cfg : invalid_cfgs_vec) {
      // since log_bin *cannot* be persisted, if must always be either CONFIG or
      // RESTART_ONLY
      using mysqlshdk::mysql::Config_types;
      assert(invalid_cfg.var_name != "log_bin" ||
             invalid_cfg.types.matches_any(
                 Config_types{mysqlshdk::mysql::Config_type::CONFIG} |
                 mysqlshdk::mysql::Config_type::RESTART_ONLY));

      invalid_cfg.types.set(mysqlshdk::mysql::Config_type::CONFIG);
    }
  }

  return invalid_cfgs_vec;
}

bool configure_instance(
    mysqlshdk::config::Config *config,
    const std::vector<mysqlshdk::mysql::Invalid_config> &invalid_configs,
    const mysqlshdk::utils::Version &version) {
  // An non-null Config with an server configuration handler is expected.
  // NOTE: a option file handler might not be needed/available.
  assert(config);
  assert(config->has_handler(mysqlshdk::config::k_dft_cfg_server_handler));

  // List of read_only variables and that cannot be persisted to be handled in
  // a custom way.
  std::vector<std::string> read_only_cfgs{"enforce_gtid_consistency",
                                          "log_slave_updates",
                                          "gtid_mode",
                                          "master_info_repository",
                                          "relay_log_info_repository",
                                          "transaction_write_set_extraction",
                                          "server_id"};

  if (version >= mysqlshdk::utils::Version(8, 0, 26)) {
    read_only_cfgs.push_back((mysqlshdk::mysql::get_replication_option_keyword(
        version, "log_slave_updates")));
  }

  std::vector<std::string> only_opt_file_cfgs{"log_bin"};

  // List of deprecated variables that should not be changed in the server
  // (might be changed in the config file)
  std::vector<std::string> deprecated_cfgs{"master_info_repository",
                                           "relay_log_info_repository"};

  // Workaround for server BUG#27629719, requiring some GR required variables
  // to be set in a certain order, namely: enforce_gtid_consistency before
  // gtid_mode. The is expected to be correct from the input parameter
  // invalid_configs and maintained. However, a delay is required to avoid them
  // from having the same timestamp in mysqld-auto.cnf when persisted.
  std::vector<std::string> persist_delay_cfgs{"enforce_gtid_consistency"};

  // Get the config server handler reference (to persist read_only variables).
  auto *srv_cfg_handler =
      dynamic_cast<mysqlshdk::config::Config_server_handler *>(
          config->get_handler(mysqlshdk::config::k_dft_cfg_server_handler));

  // Check if set persist is supported.
  log_debug("Server variable will be changed using SET PERSIST/PERSIST_ONLY.");
  bool use_set_persist = (srv_cfg_handler->get_default_var_qualifier() ==
                          mysqlshdk::mysql::Var_qualifier::PERSIST);

  // Lambda functions to set server variables using PERSIST_ONLY.
  auto set_persist_only = [&srv_cfg_handler](
                              mysqlshdk::mysql::Invalid_config &ic, uint32_t d,
                              const mysqlshdk::utils::Version &iversion) {
    if (ic.val_type == shcore::Value_type::Integer) {
      srv_cfg_handler->set(mysqlshdk::mysql::get_replication_option_keyword(
                               iversion, ic.var_name),
                           mysqlshdk::utils::nullable<int64_t>(
                               shcore::lexical_cast<int64_t>(ic.required_val)),
                           mysqlshdk::mysql::Var_qualifier::PERSIST_ONLY, d);
    } else {
      srv_cfg_handler->set(
          mysqlshdk::mysql::get_replication_option_keyword(iversion,
                                                           ic.var_name),
          mysqlshdk::utils::nullable<std::string>(ic.required_val),
          mysqlshdk::mysql::Var_qualifier::PERSIST_ONLY, d);
    }
  };

  // Set required values for incompatible configurations.
  bool need_restart = false;
  for (auto invalid_cfg : invalid_configs) {
    // Check if any of the invalid configurations requires a restart.
    if (invalid_cfg.restart) {
      need_restart = true;
    }

    // Generate server_id if one of the variables to configure.
    if (invalid_cfg.var_name == "server_id") {
      invalid_cfg.required_val =
          std::to_string(mysqlshdk::mysql::generate_server_id());
    }

    // Determine if the variable can only be changed on the option file.
    bool only_opt_file =
        std::find(only_opt_file_cfgs.begin(), only_opt_file_cfgs.end(),
                  invalid_cfg.var_name) != only_opt_file_cfgs.end();

    // Determine if the variable is read-only (to use SET PERSIST_ONLY or not
    // change it on the server).
    bool read_only_var =
        std::find(read_only_cfgs.begin(), read_only_cfgs.end(),
                  invalid_cfg.var_name) != read_only_cfgs.end();
    bool persist_only_var = use_set_persist && read_only_var;

    bool deprecated_var =
        std::find(deprecated_cfgs.begin(), deprecated_cfgs.end(),
                  invalid_cfg.var_name) != deprecated_cfgs.end();

    // Determine if the variable requires a delay for SET PERSIST.
    // Workaround related with server BUG#27629719: wait 1 ms after each
    // SET PERSIST to ensure a different timestamp is produced.
    uint32_t delay =
        (use_set_persist &&
         std::find(persist_delay_cfgs.begin(), persist_delay_cfgs.end(),
                   invalid_cfg.var_name) != persist_delay_cfgs.end())
            ? 1
            : 0;

    // Invalid configuration on the server.
    // NOTE: Skip it if it can only be changed on the option file.
    if (invalid_cfg.types.is_set(mysqlshdk::mysql::Config_type::SERVER) &&
        !only_opt_file) {
      log_debug(
          "Setting '%s' to '%s' on server (no change actually applied yet).",
          invalid_cfg.var_name.c_str(), invalid_cfg.required_val.c_str());

      // NOTE: Deprecated sysvars must not be set
      if (persist_only_var && !deprecated_var) {
        // Use SET PERSIST_ONLY for read-only variables if supported.
        // NOTE: The only variable that requires a delay is a PERSIST_ONLY one.
        set_persist_only(invalid_cfg, delay, version);
      } else if (!read_only_var && !deprecated_var) {
        // Otherwise set variable using server supported configuration, but only
        // if it is not a read_only variable.
        // NOTE: Convert value to set to the proper type (i.e., int) if needed.
        if (invalid_cfg.val_type == shcore::Value_type::Integer) {
          config->set_for_handler(
              mysqlshdk::mysql::get_replication_option_keyword(
                  version, invalid_cfg.var_name),
              mysqlshdk::utils::nullable<int64_t>(
                  shcore::lexical_cast<int64_t>(invalid_cfg.required_val)),
              mysqlshdk::config::k_dft_cfg_server_handler);
        } else {
          config->set_for_handler(
              mysqlshdk::mysql::get_replication_option_keyword(
                  version, invalid_cfg.var_name),
              invalid_cfg.required_val,
              mysqlshdk::config::k_dft_cfg_server_handler);
        }
      }
    }
    // Invalid configuration on the option file.
    // NOTE: Skip it if option file is not available.
    if (invalid_cfg.types.is_set(mysqlshdk::mysql::Config_type::CONFIG) &&
        config->has_handler(mysqlshdk::config::k_dft_cfg_file_handler)) {
      // Check if the option needs to be removed from the option file.
      // NOTE: Only applies to skip-log-bin and disable-log-bin options which
      //       do not have a corresponding server variable.
      if (invalid_cfg.required_val == mysqlshdk::mysql::k_value_not_set) {
        log_debug(
            "Removing '%s' from the option file (no change actually applied "
            "yet).",
            invalid_cfg.var_name.c_str());

        // Get the config file handler to remove the option from the file.
        auto file_cfg_handler =
            dynamic_cast<mysqlshdk::config::Config_file_handler *>(
                config->get_handler(mysqlshdk::config::k_dft_cfg_file_handler));
        file_cfg_handler->remove(invalid_cfg.var_name);
      } else {
        log_debug(
            "Setting '%s' to '%s' on option file (no change actually applied "
            "yet).",
            invalid_cfg.var_name.c_str(), invalid_cfg.required_val.c_str());
        mysqlshdk::utils::nullable<std::string> req_val{
            invalid_cfg.required_val};
        if (invalid_cfg.required_val == mysqlshdk::mysql::k_no_value) {
          // convert special string no_value to an empty nullable.
          req_val = mysqlshdk::utils::nullable<std::string>();
        }
        // Set the variable on the option file.
        config->set_for_handler(invalid_cfg.var_name, req_val,
                                mysqlshdk::config::k_dft_cfg_file_handler);
      }
    }
  }
  // Apply configuration changes.
  log_debug("Applying changes for all variables previously set.");
  config->apply();

  return need_restart;
}

void persist_gr_configurations(const mysqlshdk::mysql::IInstance &instance,
                               mysqlshdk::config::Config *config) {
  // An non-null Config with an option file configuration handler is expected.
  assert(config);
  assert(config->has_handler(mysqlshdk::config::k_dft_cfg_file_handler));

  // Get group seeds information from global var, which is supposed to have been
  // set before configureLocalInstance()

  // Get all GR configurations.
  log_debug("Get all group replication configurations.");
  std::map<std::string, mysqlshdk::utils::nullable<std::string>> gr_cfgs =
      mysqlshdk::gr::get_all_configurations(instance);

  // Set all GR configurations.
  log_debug("Set all group replication configurations to be applied.");
  // persist the GR configs using the loose_ prefix so that even if the
  // plugin is disabled, the server can start
  for (const auto &gr_cfg : gr_cfgs) {
    std::string option_name = gr_cfg.first;
    if (shcore::str_ibeginswith(option_name, "group_replication_")) {
      option_name = "loose_" + option_name;
    }
    config->set_for_handler(option_name, gr_cfg.second,
                            mysqlshdk::config::k_dft_cfg_file_handler);
  }

  // Update the group_replication_group_seeds using the live global value
  config->set("group_replication_group_seeds",
              instance.get_sysvar_string("group_replication_group_seeds"));

  // Apply all changes.
  log_debug("Apply group replication configurations (write to file).");
  config->apply();
}

void start_cluster(const mysqlshdk::mysql::IInstance &instance,
                   const Group_replication_options &gr_opts,
                   std::optional<bool> multi_primary,
                   mysqlshdk::config::Config *config) {
  assert(config);

  // Persist super_read_only=1 (but don't set it dynamically) so that the server
  // starts with it enabled in case it restarts. To avoid any possible races, we
  // must make sure the instance is never in the metadata unless sro=1 is
  // persisted (except in 5.7).
  config->set("super_read_only", mysqlshdk::utils::nullable<bool>(true));

  // Check if offline_mode is enabled to disable it
  if (instance.get_sysvar_bool("offline_mode").get_safe()) {
    log_info("Disabling offline_mode on '%s'", instance.descr().c_str());
    config->set("offline_mode", mysqlshdk::utils::nullable<bool>(false));
  }

  mysqlshdk::utils::nullable<bool> single_primary_mode;
  if (multi_primary) single_primary_mode = !*multi_primary;

  // Set GR replication options:
  // - primary mode (topology mode) set by the user or using default (resolved
  //   by the caller);
  //   NOTE: Can be null and not set (e.g., reboot cluster).
  // - group name set by the user or automatically generated (resolved by the
  //   caller);
  // - SSL variable according to the ssl_mode value (resolved by the caller);
  // - local_address determined based on the given localAddress option
  //   (resolved by the caller);
  // - group_seeds (if provided);
  // - IP whitelist (if provided);
  // - exit state action (if provided);
  // - member weight (if provided);
  // - (failover) consistency (if provided);
  // - expel timeout (if provided);
  // - auto-rejoin tries (if provided);
  // - Enable GR start on boot;
  // - communication stack (if provided).
  // - transactionSizeLimit (if provided)
  set_gr_options(instance, gr_opts, config, single_primary_mode);

  if (multi_primary) {
    // Set auto-increment settings (depending on the topology type) on the seed
    // instance (assuming it will be successfully start the group; size = 1).
    mysqlshdk::gr::Topology_mode topology_mode =
        (*multi_primary) ? mysqlshdk::gr::Topology_mode::MULTI_PRIMARY
                         : mysqlshdk::gr::Topology_mode::SINGLE_PRIMARY;
    log_debug(
        "Setting auto-increment values for topology mode '%s' and group size "
        "1.",
        mysqlshdk::gr::to_string(topology_mode).c_str());
    mysqlshdk::gr::update_auto_increment(config, topology_mode, 1);
  }

  // Apply all configuration changes.
  log_debug("Applying configuration change to instance '%s'.",
            instance.descr().c_str());
  config->apply();

  log_debug("Starting Group Replication to bootstrap group...");
  start_group_replication(instance, true);

  wait_super_read_only_cleared(instance);

  log_debug("Instance '%s' successfully started the Group Replication group.",
            instance.descr().c_str());
}

void join_cluster(const mysqlshdk::mysql::IInstance &instance,
                  const mysqlshdk::mysql::IInstance &peer_instance,
                  const Group_replication_options &gr_opts,
                  const mysqlshdk::utils::nullable<uint64_t> &cluster_size,
                  mysqlshdk::config::Config *config) {
  // An non-null Config is expected.
  assert(config);

  // Persist super_read_only=1 so that the server starts with it enabled
  // in case it restarts. To avoid any possible races, we must make sure the
  // instance is never in the metadata unless sro=1 is persisted
  config->set("super_read_only", mysqlshdk::utils::nullable<bool>(true));

  // Check if offline_mode is enabled to disable it
  if (instance.get_sysvar_bool("offline_mode").get_safe()) {
    log_info("Disabling offline_mode on '%s'", instance.descr().c_str());
    config->set("offline_mode", mysqlshdk::utils::nullable<bool>(false));
  }

  std::string gr_group_name, gr_view_change_uuid;
  bool single_primary_mode;
  mysqlshdk::gr::Member_state peer_state;

  // Get required information from the peer_instance (namely the group_name).
  log_debug("Getting information from peer instance '%s'.",
            peer_instance.descr().c_str());
  if (!mysqlshdk::gr::get_group_information(
          peer_instance, &peer_state, nullptr, &gr_group_name,
          &gr_view_change_uuid, &single_primary_mode)) {
    throw std::runtime_error{"Cannot join instance '" + instance.descr() +
                             "'. Peer instance '" + peer_instance.descr() +
                             "' is no longer a member of the cluster."};
  }

  // Confirm the peer_instance status, must be ONLINE.
  if (peer_state != mysqlshdk::gr::Member_state::ONLINE) {
    throw std::runtime_error(
        "Cannot join instance '" + instance.descr() + "'. Peer instance '" +
        peer_instance.descr() + "' state is currently '" +
        mysqlshdk::gr::to_string(peer_state) +
        "', but is expected to "
        "be '" +
        mysqlshdk::gr::to_string(mysqlshdk::gr::Member_state::ONLINE) + "'.");
  }

  // Set GR options:
  // - primary mode (topology mode) obtained from the peer instance;
  // - group name obtained from the peer instance;
  // - SSL variable according to the ssl_mode value (resolved by the caller);
  // - local_address determined based on the given localAddress option
  //   (resolved by the caller);
  // - group_seeds as set by the caller
  // - IP whitelist (if provided);
  // - exit state action (if provided);
  // - member weight (if provided);
  // - (failover) consistency (if provided);
  // - expel timeout (if provided);
  // - auto-rejoin tries (if provided);
  // - Enable GR start on boot;
  set_gr_options(instance, gr_opts, config,
                 mysqlshdk::utils::nullable<bool>(single_primary_mode),
                 gr_group_name, gr_view_change_uuid);

  // Set auto-increment settings (depending on the topology type and size of
  // the group) on the instance to add (assuming it will be successfully join).
  if (!cluster_size.is_null()) {
    mysqlshdk::gr::Topology_mode topology_mode =
        (single_primary_mode) ? mysqlshdk::gr::Topology_mode::SINGLE_PRIMARY
                              : mysqlshdk::gr::Topology_mode::MULTI_PRIMARY;
    log_debug(
        "Setting auto-increment values for topology mode '%s' and group size "
        "%s.",
        mysqlshdk::gr::to_string(topology_mode).c_str(),
        std::to_string(*cluster_size + 1).c_str());
    mysqlshdk::gr::update_auto_increment(config, topology_mode,
                                         *cluster_size + 1);
  }

  // Apply all configuration changes.
  log_debug("Applying configuration change to instance '%s'.",
            instance.descr().c_str());
  config->apply();

  // Set the GR replication (recovery) user if specified (not empty).
  if (gr_opts.recovery_credentials &&
      !gr_opts.recovery_credentials->user.empty()) {
    log_debug("Setting Group Replication recovery user to '%s'.",
              gr_opts.recovery_credentials->user.c_str());
    mysqlshdk::mysql::change_replication_credentials(
        instance, gr_opts.recovery_credentials->user,
        gr_opts.recovery_credentials->password.get_safe(),
        mysqlshdk::gr::k_gr_recovery_channel);
  }

  log_debug("Starting Group Replication to join group...");
  start_group_replication(instance, false);

  log_debug("Instance '%s' successfully joined the Group Replication group.",
            instance.descr().c_str());
}

}  // namespace dba
}  // namespace mysqlsh