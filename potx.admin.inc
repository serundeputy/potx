<?php

/**
 * @file
 *   Administrative interface for the module.
 */

/**
 * Component selection interface.
 */
function potx_select_component_form() {

  $form = array();
  $components = _potx_component_list();
  _potx_component_selector($form, $components);

  // Generate translation file for a specific language if possible.
  $languages = language_list();
  if (count($languages) > 1 || !isset($languages['en'])) {
    // We have more languages, or the single language we have is not English.
    $options = array('n/a' => t('Language independent template'));
    foreach ($languages as $langcode => $language) {
      // Skip English, as we should not have translations for this language.
      if ($langcode == 'en') {
        continue;
      }
      $options[$langcode] = t('Template file for !langname translations', array('!langname' => t($language->name)));
    }
    $form['langcode'] = array(
      '#type' => 'radios',
      '#title' => t('Template language'),
      '#default_value' => 'n/a',
      '#options' => $options,
      '#description' => t('Export a language independent or language dependent (plural forms, language team name, etc.) template.'),
    );
    $form['translations'] = array(
      '#type' => 'checkbox',
      '#title' => t('Include translations'),
      '#description' => t('Include translations of strings in the file generated. Not applicable for language independent templates.')
    );
  }

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Extract'),
  );

  return $form;
}

/**
 * Validation handler for potx component selection form.
 */
function potx_select_component_form_validate($form, &$form_state) {
  if (empty($form_state['values']['component'])) {
    form_set_error('', t('You should select a component to export.'));
  }
}

/**
 * Generate translation template or translation file for the requested component.
 */
function potx_select_component_form_submit($form, &$form_state) {
  global $devel_shutdown;

  // Avoid devel.module putting extra output to the end of files exported.
  $devel_shutdown = FALSE;

  // This could take some time.
  @set_time_limit(0);
  module_load_include('inc', 'potx');
  module_load_include('inc', 'potx', 'potx.local');

  // Silence status messages.
  potx_status('set', POTX_STATUS_MESSAGE);

  // $form_state['values']['component'] either contains a specific file name
  // with path, or a directory name for a module/theme or a module suite.
  // Examples:
  //   modules/watchdog
  //   sites/all/modules/coder
  //   sites/all/modules/i18n/i18n.module
  //   themes/garland

  $component = $form_state['values']['component'];
  $pathinfo = pathinfo($component);
  if (!isset($pathinfo['filename'])) {
    // The filename key is only available in PHP 5.2.0+
    $pathinfo['filename'] = substr($pathinfo['basename'], 0, strrpos($pathinfo['basename'], '.'));
  }

  if (isset($pathinfo['extension'])) {
    // A specific module or theme file was requested (otherwise there should be no extension).
    potx_local_init($pathinfo['dirname']);
    $files = _potx_explore_dir($pathinfo['dirname'] .'/', $pathinfo['filename']);
    $strip_prefix = 1 + strlen($pathinfo['dirname']);
    $outputname = $pathinfo['filename'];
  }
  // A directory name was requested.
  else {
    potx_local_init($component);
    $files = _potx_explore_dir($component .'/');
    $strip_prefix = 1 + strlen($component);
    $outputname = $pathinfo['basename'];
  }

  // Decide on template or translation file generation.
  $template_langcode = $translation_langcode = NULL;
  if (isset($form_state['values']['langcode']) && ($form_state['values']['langcode'] != 'n/a')) {
    $template_langcode = $form_state['values']['langcode'];
    $outputname .= '.'. $template_langcode;
    if (!empty($form_state['values']['translations'])) {
      $translation_langcode = $template_langcode;
      $outputname .= '.po';
    }
    else {
      $outputname .= '.pot';
    }
  }
  else {
    $outputname .= '.pot';
  }

  // Collect every string in affected files. Installer related strings are discared.
  foreach ($files as $file) {
    _potx_process_file($file, $strip_prefix);
  }

  // Need to include full parameter list to get to passing the language codes.
  _potx_build_files(POTX_STRING_RUNTIME, POTX_BUILD_SINGLE, 'general', '_potx_save_string', '_potx_save_version', '_potx_get_header', $template_langcode, $translation_langcode);

  _potx_write_files($outputname, 'attachment');

  exit;
}

/**
 * Build a chunk of the component selection form.
 *
 * @param $form
 *   Form to populate with fields.
 * @param $components
 *   Structured array with components as returned by _potx_component_list().
 * @param $dirname
 *   Name of directory handled.
 */
function _potx_component_selector(&$form, &$components, $dirname = '') {

  // Pop off count of components in this directory.
  if (isset($components['#-count'])) {
    $component_count = $components['#-count'];
    unset($components['#-count']);
  }

  //ksort($components);
  $dirkeys = array_keys($components);

  // A directory with one component.
  if (isset($component_count) && (count($components) == 1)) {
    $component = array_shift($components);
    $dirname = dirname($component->filename);
    $form[_potx_form_id('dir', $dirname)] = array(
      '#type' => 'radio',
      '#title' => t('Extract from %name in the %directory directory', array('%directory' => $dirname, '%name' => $component->name)),
      '#description' => t('Generates output from all files found in this directory.'),
      '#default_value' => 0,
      '#return_value' => $dirname,
      // Get all radio buttons into the same group.
      '#parents' => array('component'),
    );
    return;
  }

  // A directory with multiple components in it.
  if (preg_match('!/(modules|themes)\\b(/.+)?!', $dirname, $pathmatch)) {
    $t_args = array('@directory' => substr($dirname, 1));
    if (isset($pathmatch[2])) {
      $form[_potx_form_id('dir', $dirname)] = array(
        '#type' => 'radio',
        '#title' => t('Extract from all in directory "@directory"', $t_args),
        '#description' => t('To extract from a single component in this directory, choose the desired entry in the fieldset below.'),
        '#default_value' => 0,
        '#return_value' => substr($dirname, 1),
        // Get all radio buttons into the same group.
        '#parents' => array('component'),
      );
    }
    $element = array(
      '#type' => 'fieldset',
      '#title' => t('Directory "@directory"', $t_args),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $form[_potx_form_id('fs', $dirname)] =& $element;
  }
  else {
    $element =& $form;
  }

  foreach ($dirkeys as $entry) {
    // A component in this directory with multiple components.
    if ($entry[0] == '#') {
      // Component entry.
      $t_args = array(
        '%directory' => dirname($components[$entry]->filename),
        '%name'      => $components[$entry]->name,
        '%pattern'   => $components[$entry]->name .'.*',
      );
      $element[_potx_form_id('com', $components[$entry]->basename)] = array(
        '#type' => 'radio',
        '#title' => t('Extract from %name', $t_args),
        '#description' => t('Extract from files named %pattern in the %directory directory.', $t_args),
        '#default_value' => 0,
        '#return_value' => $components[$entry]->filename,
        // Get all radio buttons into the same group.
        '#parents' => array('component'),
      );
    }
    // A subdirectory we need to look into.
    else {
      _potx_component_selector($element, $components[$entry], "$dirname/$entry");
    }
  }

  return count($components);
}

/**
 * Generate a sane form element ID for the current radio button.
 *
 * @param $type
 *   Type of ID generated: 'fs' for fieldset, 'dir' for directory, 'com' for component.
 * @param $path
 *   Path of file we generate an ID for.
 * @return string
 *   The generated ID.
 */
function _potx_form_id($type, $path) {
  return 'potx-'. $type .'-'. preg_replace('/[^a-zA-Z0-9]+/', '-', $path);
}

/**
 * Generate a hierarchical structured list of components.
 *
 * @return array
 *  Array in the directory structure identified.
 *    - 'normal'  keyed elements being subfolders
 *    - '#name'   elements being component objects for the 'name' component
 *    - '#-count' being the file count of all components in the directory
 */
function _potx_component_list() {
  $components = array();
  // Get a list of all enabled modules and themes.
  $result = db_query("SELECT name, filename, type, status FROM {system} WHERE type IN ('module', 'theme') ORDER BY filename ASC");
  foreach ($result as $component) {
    // Build directory tree structure.
    $path_parts = explode('/', dirname($component->filename));
    $dir =& $components;
    foreach ($path_parts as $dirname) {
      if (!isset($dir[$dirname])) {
        $dir[$dirname] = array();
      }
      $dir =& $dir[$dirname];
    }

    // Information about components in this directory.
    $component->basename = basename($component->filename);
    $dir['#'. $component->basename] = $component;
    $dir['#-count'] = isset($dir['#-count']) ? $dir['#-count'] + 1 : 1;
  }

  return $components;
}
