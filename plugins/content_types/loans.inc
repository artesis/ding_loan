<?php

/**
 * Plugins are described by creating a $plugin array which will be used
 * by the system that includes this file.
 */
$plugin = array(
  'title' => t('User loan list'),
  'description' => t('The current loans for a user'),
  'single' => TRUE,
  'content_types' => array('list'),
  'required context' => new ctools_context_required(t('User'), 'user'),
  'category' => t('User'),
);

/**
 * Render the block
 */
function ding_loan_loans_content_type_render($subtype, $conf, $panel_args, $context) {
  // Define basic block object.
  $block = new stdClass();
  $block->module = 'ding_loan';
  $block->delta = 'loans';
  $block->title = t('Loan list');

  // Get loans from the provider.
  $account = isset($context->data) ? $context->data : NULL;
  $loans = ding_provider_invoke_page('loan', 'list', $account);

  // Set block content, with loans form.
  $block->content = ding_provider_get_form('ding_loan_loans_form', $account, $loans);

  return $block;
}

/**
 * Adding the admin form, to be able to control the required context
 */
function ding_loan_loans_content_type_edit_form($form, &$form_state) {
  return $form;
}

/**
 * Implements a drupal form, which show a list of reservations
 */
function ding_loan_loans_form($form, &$form_state, $account, $loans = array()) {
  $options = array();

  $header = array(
    'title' => t('Title'),
    'loan_date' => t('Loan date'),
    'expiry' => t('Return date'),
    'operations' => '',
  );

  // Sort loans base on exipre date.
  uasort($loans, 'ding_loan_sort_expiry');

  // Store account information in the form (on server site).
  $form['account'] = array(
    '#type' => 'value',
    '#value' => $account,
  );

  // Store the loan list in the form (on server site).
  $form['items'] = array(
    '#type' => 'value',
    '#value' => $loans,
  );

  foreach ($loans as $id => $item) {
    $item->creator = 'fake';
    $entity = $item->entity;
    $disabled = $item->renewable ? FALSE : TRUE;

    if (!is_object($entity)) {
      // @todo Add dependency or refactor it.
      $entity = ding_reservation_replace_entity($item);
    }

    $options[$item->id] = array(
      '#disabled' => $disabled,
      'title' => array(
        'data' => array(
          $entity ? ting_object_view($entity, 'user_list') : array('#markup' => $item->display_name),
        ),
        'class' => 'title',
      ),
      'loan_date' => array(
        'data' => format_date(strtotime(check_plain($item->loan_date)), 'custom', 'd/m/Y'),
        'class' => 'created-date',
      ),
      'expiry' => array(
        'data' => format_date(strtotime(check_plain($item->expiry)), 'custom', 'd/m/Y'),
        'class' => 'expire-date',
      ),
      'operations' => array(
        'data' => array(
        ),
        'class' => 'operations',
      ),
    );

    // Add material number.
    if (isset($item->materials_number)) {
      $options[$item->id]['title']['data'][] = array(
        '#type' => 'markup',
        '#prefix' => '<p class="order-id">',
        '#markup' => t('Material no. @mat_no', array('@mat_no' => $item->materials_number)),
        '#suffix' => '</p>',
      );
    }

    // Add renew load opreation.
    if ($disabled == FALSE) {
      $options[$item->id]['operations']['data'][] = array(
        '#type' => 'submit',
        '#prefix' => '<div class="renew-loan-button">',
        '#value' => t('Renew'),
        '#loan_id' => $item->id,
        '#name' => 'renew-' . $item->id, // Needed so form API can tell them apart.
        '#suffix' => '</div>',
        '#submit' => array('ding_loan_loans_form_submit'),
        '#executes_submit_callback' => TRUE,
      );
    }
  }

  $form['actions_top'] = array (
    '#weight' => 0,
  );
  $form['actions_top']['submit_first'] = array(
    '#type' => 'submit',
    '#prefix' => '<div class="renew-loan-button">',
    '#value' => t('Renew selected'),
    '#suffix' => '</div>',
  );

  $form['loans'] = array(
    '#type' => 'tableselect_form',
    '#header' => $header,
    '#options' => $options,
    '#empty' => t('No loans'),
    '#process' => array('ding_base_element_process_tableselect_form', '_ding_loan_loans_disable'),
    '#_choice_last' => TRUE,
    '#_select_all_label' => t('Select all'),
  );

  $form['actions_bottom'] = array (
    '#weight' => 2,
  );
  $form['actions_bottom']['submit_second'] = array(
    '#type' => 'submit',
    '#prefix' => '<div class="renew-loan-button">',
    '#value' => t('Renew selected'),
    '#name' => 'renew_all',
    '#suffix' => '</div>',
  );

  return $form;
}


/**
 * Process function to disable checkboxes for unrenewable materials.
 */
function _ding_loan_loans_disable($element) {
  foreach ($element['#options'] as $key => $choice) {
    if (isset($choice['#disabled']) && $choice['#disabled'] == TRUE) {
      $element[$key]['#attributes']['title'] = t('Not renewable');
    }
  }
  return $element;
}

/**
 * Submit handler for the form.
 */
function ding_loan_loans_form_submit($form, &$form_state) {
  $ids = array();
  foreach ($form_state['values']['items'] as $item) {
    $ids[] = $item->id;
  }
  if (empty($ids)) {
    return;
  }
  $error_messages = array(
    DingProviderLoan::STATUS_NOT_RENEWED => t('Unable to renew material'),
    DingProviderLoan::STATUS_RENEWAL_NUM_EXCEEDED => t('Maximum number of renewals reached'),
    DingProviderLoan::STATUS_RENEWAL_RESERVED => t('The material is reserved by another loaner'),
  );
  $clear_cache = FALSE;

  $result = ding_provider_invoke('loan', 'renew', $form_state['values']['account'], $ids);
  foreach ($result as $id => $status) {
    // Try to get entity for the item.
    $entity = $form_state['values']['items'][$id]->entity;
    if (!is_object($entity) || $entity->properties === FALSE) {
      $item = $form_state['values']['items'][$id];
      // Build pseudo entity based on provider information.
      $title = $item->getTitle() ? $item->getTitle() : $item->__get('display_name');
    }
    else {
      // Create title from entity object.
      $title = $entity->getTitle();
    }
    // @todo Better feedback.
    switch ($status) {
      case DingProviderLoan::STATUS_RENEWED:
      case DingProviderLoan::STATUS_RENEWAL_REQUESTED:
        drupal_set_message(t('"@title renewed"', array('@title' => $title)));
        $clear_cache = TRUE;
        break;
      case DingProviderLoan::STATUS_NOT_RENEWED:
      case DingProviderLoan::STATUS_RENEWAL_NUM_EXCEEDED:
      case DingProviderLoan::STATUS_RENEWAL_RESERVED:
        drupal_set_message(t('"@title: @message"', array(
          '@title' => $title,
          '@message' => $error_messages[$status],
        )), 'warning');
        break;
      default:
        watchdog('ding_loan', 'Unknown renewal status code @code.', array('@code' => $status), WATCHDOG_ERROR);
    }
  }
  // Clear session cache.
  if ($clear_cache && module_exists('ding_session_cache')) {
    ding_session_cache_clear('ding_loan', 'list');
  }
}
