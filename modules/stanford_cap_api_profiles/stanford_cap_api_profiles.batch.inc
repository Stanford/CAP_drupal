<?php
/**
 * @file
 * Batch profiles & groups import batch callback functions and helpers.
 *
 * Use stanford_cap_api_profiles_import_batch()
 * or stanford_cap_api_profiles_groups_import_batch() to create a batch.
 */

/**
 * Batch finished callback.
 *
 * Show and write import results to db.
 */
function stanford_cap_api_profiles_groups_import_batch_finished($success, $results, $operations) {
  db_query(
    'DELETE FROM {cap_api_import_cache} WHERE importer = :importer',
    array(':importer' => 'stanford_cap_api_profiles_groups_import_batch_data_request')
  );

  if ($success) {
    $group = $results['group'];
    stanford_cap_api_profiles_sync_group_save(
      $group['type'],
      $group['name'],
      $results['profilesImported'],
      REQUEST_TIME,
      $group['sync_children']
    );

    $message = $results['profilesProcessed'] . ' profiles processed.';
    if (!empty($results['profilesImported'])) {
      $message .= '</br>' . $results['profilesImported'] . ' profiles imported successfully.';
    }
    if (!empty($results['profilesImportFailed'])) {
      $message .= '</br>' . $results['profilesImportFailed'] . ' profiles import faild.';
    }
    drupal_set_message($message);

    if (!empty($context['results']['errors'])) {
      drupal_set_message(
        t(
          'The was an errors while batch process, check @log for details',
          array('@log' => l(t('Recent log messages'), 'admin/reports/dblog'))
        ),
        'warning'
      );
    }

  }
  else {
    // An error occurred.
    // $operations contains the operations that remained unprocessed.
    $error_operation = reset($operations);
    $message = t(
      'An error occurred while processing %error_operation with arguments: @arguments',
      array('%error_operation' => $error_operation[0], '@arguments' => print_r($error_operation[1], TRUE))
    );
    drupal_set_message($message, 'error');
  }
}

/**
 * Groups import batch operation callback.
 */
function stanford_cap_api_profiles_groups_import_batch_start($request_params, $group, &$context) {
  $context['message'] = 'Requesting data from server.';

  $context['results']['request_params'] = $request_params;
  $context['results']['group'] = $group;
  $context['results']['profilesProcessed'] = 0;
  $context['results']['profilesImported'] = 0;
  $context['results']['profilesImportFailed'] = 0;
  $context['results']['totalRequests'] = 0;
  $context['results']['currentRequest'] = 0;
  $context['results']['profilesToImport'] = array();

  $data = stanford_cap_api_request('/profiles/v1', $request_params);
  if ($data && $data['totalCount'] > 0) {
    $context['results']['totalRequests'] = $data['totalPages'];
    $context['results']['currentRequest'] = 1;
    $context['results']['profilesCountToImport'] = count($data['values']);
    $context['results']['profilesToImport'] = $data['values'];
    $context['results']['profilesTotalCount'] = $data['totalCount'];

    $context['message'] = t(
      '@count profiles queued for import.',
      array('@count' => count($context['results']['profilesToImport']))
    );
  }
  else {
    watchdog(
      'stanford_cap_api_profiles batch',
      'There was an issue while retrieving data from CAP service by request "@request".',
      array('@request' => var_export($request_params, TRUE))
    );
  }
}

/**
 * Groups import batch operation callback.
 *
 * Requesting data portions from CAP API server and put it to import queue.
 *
 * Import queue stored now in $context(RAM) but db may be preferable
 * for large data.
 */
function stanford_cap_api_profiles_groups_import_batch_data_request(&$context) {
  if ($context['results']['currentRequest'] == $context['results']['totalRequests']) {
    return;
  }

  $request_params = $context['results']['request_params'];
  $request_params['p'] = ++$context['results']['currentRequest'];
  $data = stanford_cap_api_request('/profiles/v1', $request_params, TRUE);
  $metadata = json_decode($data, TRUE);
  if (!empty($metadata['values']) && is_array($metadata['values'])) {
    $context['results']['profilesCountToImport'] += count($metadata['values']);
    $record = array(
      'importer' => 'stanford_cap_api_profiles_groups_import_batch_data_request',
      'data' => $data,
    );
    drupal_write_record('cap_api_import_cache', $record);
    $context['message'] = t(
      '@count profiles queued for import of @total.',
      array(
        '@count' => $context['results']['profilesCountToImport'],
        '@total' => $context['results']['profilesTotalCount'],
      )
    );
  }
  else {
    $context['results']['errors'] = TRUE;
    watchdog(
      'stanford_cap_api_profiles batch',
      'There was an issue while retrieving data from CAP service by request "@request".',
      array('@request' => var_export($request_params, TRUE))
    );
  }

  $context['finished'] = $context['results']['currentRequest'] / $context['results']['totalRequests'];
}

/**
 * Groups import batch operation callback.
 *
 * Pop profile from import queue and import it.
 */
function stanford_cap_api_profiles_groups_import_batch_data_import(&$context) {
  $context['message'] = t(
    '@count profiles processed of @total.',
    array(
      '@count' => $context['results']['profilesProcessed'],
      '@total' => $context['results']['profilesCountToImport'],
    )
  );

  if (empty($context['results']['profilesToImport'])) {
    $result = db_query(
      'SELECT cid, data from {cap_api_import_cache} WHERE importer = :importer LIMIT 0 , 1',
      array(':importer' => 'stanford_cap_api_profiles_groups_import_batch_data_request')
    );
    $record = $result->fetchObject();
    if (!empty($record)) {
      $data = json_decode($record->data, TRUE);
      if (is_array($data)) {
        db_query('DELETE FROM {cap_api_import_cache} WHERE cid = :cid', array(':cid' => $record->cid));
        $context['results']['profilesToImport'] = $data['values'];
      }
      else {
        $context['results']['errors'] = TRUE;
        watchdog(
          'stanford_cap_api_profiles batch',
          'Error restoring CAP data from local cache. Cache ID: @cid.',
          array('@cid' => $record->cid),
          WATCHDOG_ERROR
        );
        // Default quantity of profiles returned by CAP.
        $context['results']['profilesImportFailed'] += 10;
      }

    }
    else {
      $context['message'] = 'Import complite';

      return;
    }
  }

  $profile = array_pop($context['results']['profilesToImport']);
  if (!empty($profile)) {
    try {
      stanford_cap_api_profiles_profile_import($profile);
      $context['results']['profilesImported']++;
    }
    catch (Exception $e) {
      $context['results']['errors'] = TRUE;
      watchdog(
        'stanford_cap_api_profiles batch',
        'Exception while profile @profile_id import. ' . $e->getMessage(),
        array('@profile_id' => $profile['profileId']),
        WATCHDOG_ERROR
      );
      $context['results']['profilesImportFailed']++;
    }
  }

  $context['results']['profilesProcessed']++;
  $context['finished'] = $context['results']['profilesProcessed'] / $context['results']['profilesCountToImport'];
}

/**
 * Helper creating a $batch conf array to import groups.
 */
function stanford_cap_api_profiles_groups_import_batch($group) {
  $operations = array();
  switch ($group['type']) {
    case 'organization':
      $request_params = array('orgCodes' => $group['name'], 'includeChildren' => $group['sync_children']);
      break;

    case 'workgroup':
      $request_params = array('privGroups' => $group['name']);
      break;

    default:
      throw new Exception('Unknown or undefined group type passed.');
  }
  $operations[] = array(
    'stanford_cap_api_profiles_groups_import_batch_start',
    array($request_params, $group),
  );
  $operations[] = array(
    'stanford_cap_api_profiles_groups_import_batch_data_request',
    array(),
  );
  $operations[] = array(
    'stanford_cap_api_profiles_groups_import_batch_data_import',
    array(),
  );

  $batch = array(
    'file' => drupal_get_path('module', 'stanford_cap_api_profiles') . '/stanford_cap_api_profiles.batch.inc',
    'title' => t('Synchronizing group profiles'),
    'operations' => $operations,
    'finished' => 'stanford_cap_api_profiles_groups_import_batch_finished',
    'progress_message' => t(
      'Current: @current | Remaining: @remaining | Total: @total | Estimate: @estimate | Elapsed: @elapsed'
    ),
  );

  return $batch;
}

/**
 * Helper creating a $batch conf array to import profiles.
 */
function stanford_cap_api_profiles_import_batch(array $profile_ids) {
  // Filter out all empty values.
  $profile_ids = array_filter($profile_ids);
  $operations = array();
  foreach ($profile_ids as $profile_id) {
    $operations[] = array(
      'stanford_cap_api_profiles_profile_import',
      array($profile_id),
    );
  }

  $batch = array(
    'file' => drupal_get_path('module', 'stanford_cap_api_profiles') . '/stanford_cap_api_profiles.batch.inc',
    'title' => t('Importing profiles'),
    'operations' => $operations,
    'progress_message' => t(
      'Current: @current | Remaining: @remaining | Total: @total | Estimate: @estimate | Elapsed: @elapsed'
    ),
  );

  return $batch;
}
