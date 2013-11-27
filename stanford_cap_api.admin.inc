<?php
/**
 * @file
 * Administrative pages for Stanford CAP API module.
 */

/**
 * Form builder for details page.
 */
function stanford_cap_api_details_form($form, &$form_state) {
  $form['#attached']['css'][] = drupal_get_path('module', 'stanford_cap_api') . '/css/stanford_cap_api.css';

  // Status message.
  $status = stanford_cap_api_auth_status();
  if ($status) {
    $status_message = t('Currently, your site can connect to CAP.');
    $additional = NULL;
  }
  else {
    $status_message = t("Currently, your site can't connect to CAP.");
    $additional = t('Please check your settings on the !link.', array('!link' => l(t('settings page'), 'admin/config/cap/config/settings')));
  }
  $vars = array(
    'status' => $status,
    'message' => $status_message,
    'additional' => $additional,
  );
  $form['status'] = array(
    '#markup' => theme('cap_status_item', $vars),
  );

  return $form;
}

/**
 * Form builder for settings page.
 */
function stanford_cap_api_settings_form($form, &$form_state) {

  drupal_set_title(t('CAP API settings'));

  $form['connection'] = array(
    '#type' => 'fieldset',
    '#title' => t('Connection information'),
  );

  $form['connection']['description_wrapper']['description'] = array(
    '#type' => 'item',
    '#markup' => t('Please enter the information you use to connect to the CAP API. This will be used for each callback to the CAP API itself.'),
  );

  $form['connection']['cap_username'] = array(
    '#type' => 'textfield',
    '#title' => t('Username:'),
    '#default_value' => variable_get('cap_username', ''),
    '#required' => TRUE,
  );

  $form['connection']['cap_password'] = array(
    '#type' => 'password',
    '#title' => t('Password:'),
  );

  $form['connection']['advanced'] = array(
    '#type' => 'fieldset',
    '#title' => t('Advanced'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#description' => t('Advanced setting for CAP API and authentication URIs'),
  );

  $form['connection']['advanced']['cap_api_base_url'] = array(
    '#type' => 'textfield',
    '#title' => t('Endpoint'),
    '#description' => t('CAP API endpoint URI, only useful when switching between development/production environment.'),
    '#default_value' => variable_get('cap_api_base_url', 'https://cap.stanford.edu/cap-api'),
    '#required' => TRUE,
  );

  $form['connection']['advanced']['cap_auth_uri'] = array(
    '#type' => 'textfield',
    '#title' => t('Authentication URI'),
    '#description' => t('CAP API authentication URL.'),
    '#default_value' => variable_get('cap_auth_uri', 'https://authz.stanford.edu/oauth/token'),
    '#required' => TRUE,
  );

  $form['actions'] = array('#type' => 'actions');
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save settings'),
  );

  return $form;
}

/**
 * Validation handler for settings form.
 */
function stanford_cap_api_settings_form_validate($form, &$form_state) {
  if (!empty($form_state['values']['cap_username']) && !empty($form_state['values']['cap_password'])) {
    $username = $form_state['values']['cap_username'];
    $password = $form_state['values']['cap_password'];
    $auth_uri = $form_state['values']['cap_auth_uri'];
    $auth_token = stanford_cap_api_auth($username, $password, $auth_uri);
    if (!$auth_token) {
      form_set_error('cap_username', t("Error. Can't connect to Stanford CAP API. Please check your username and password."));
      form_set_error('cap_password');
    }
  }
}

/**
 * Submit handler for settings form.
 */
function stanford_cap_api_settings_form_submit($form, &$form_state) {
  $config_vars = array(
    'cap_username',
    'cap_password',
    'cap_api_base_url',
    'cap_auth_uri',
  );
  $values = $form_state['values'];
  foreach ($config_vars as $config_var) {
    if (!empty($values[$config_var])) {
      variable_set($config_var, $values[$config_var]);
    }
  }
  drupal_set_message(t('Configuration saved.'));
}

/**
 * Checks that we can connect to CAP API.
 */
function stanford_cap_api_auth_status() {
  if (stanford_cap_api_request('/api/profiles/v1')) {
    return TRUE;
  }
  else {
    return FALSE;
  }
}
