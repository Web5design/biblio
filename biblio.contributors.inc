<?php
//$Id$
/**
 * @param $authors
 * @param $withLinks
 * @return unknown_type
 */
function biblio_get_contributor_strings($authors, $withLinks = TRUE) {
  if (count($authors)) {
    foreach ($authors as $key => $author) {
      $author_array[$author['ct_id']] .= (!empty ($author_array[$author['ct_id']])) ? '; ' . $author['name'] : $author['name']; // $author['firstname'] .' '. $author['initials'].' '.$author['lastname'];
    }
  }
  if (count($author_array)) {
    //ksort($author_array);
    return $author_array;
  }
  else {
    return array(
    1 => t('No author information available'
    ));
  }
}
/**
 * @param $aid
 * @return unknown_type
 */
function biblio_get_contributor($aid) {
  static $contributor = array();
  if (!isset($contributor[$aid])) {
    $contributor[$aid] = db_fetch_object(db_query('SELECT * FROM {biblio_contributor_data} WHERE cid = %d', $aid));
  }

  return $contributor[$aid];
}

/**
 * @param $vid
 * @return unknown_type
 */
function biblio_load_contributors($vid, $biblio_type) {
  $contributors = array();
  $query = 'SELECT bcd.*, bct.auth_catagory,  bct.auth_type, bc.rank
    				  FROM    {biblio_contributor} bc,
    					 {biblio_contributor_data} bcd,
    					 {biblio_contributor_type} bct
    				  WHERE bc.vid = %d
    				     AND bct.biblio_type = %d
    				  	 AND bc.cid = bcd.cid
    				  	 AND  bc.auth_type = bct.auth_type
    				  ORDER BY bct.auth_type ASC, bc.rank ASC;';

  $result = db_query($query, array($vid, $biblio_type));
  while ($creator = db_fetch_array($result)) {
    $contributors[] = $creator;
  }
  return $contributors;
}
/**
 * Add separate author named "et al" to the end of the author array
 * 
 * @param $authors - author array to augment
 * @param $type - auth_type
 * @return true if author was added, false if "etal" was already there
 */
function biblio_authors_add_etal (&$authors, $type) {
  $etal = "et al";
  foreach ($authors as $author) {
    if ($author['auth_type'] == $type && $author['name'] == $etal) return false;
  }
  $authors[] = array('name' => $etal, 'auth_type' => $type, 'lastname' => $etal);
  return true;
}
/**
 * Parse initial contributor array and augment with additional info
 * @param $contributors initial contributor array
 * @return augmented contributor array
 */
function biblio_parse_contributors($contributors) {
  $authors = array(); $etal = array();
  foreach ($contributors as $author) {
    $author['name'] = trim($author['name']); 
    if (strlen($author['name'])) {
      $count=0;
      // remove any form of "et al" from name field, because it confuses biblio_parse_author
      $author['name'] = preg_replace("/et\.?\s+al\.?/",'',$author['name'],-1,$count);
      // if "et al" was present, mark it as "to be added" in $etal array
      if ($count && $author['auth_type'] != 1) $etal[$author['auth_type']] = true;

      biblio_parse_author($author);
      $authors[] = (array) $author;
    }
  }
  // add "et al" authors for all neccessary author types
  foreach ($etal as $type => $dummy) {
    if (biblio_authors_add_etal ($authors, $type))
      biblio_parse_author($authors[sizeof($authors)-1]);
  }
  return $authors;
}
/**
 * Save $node->biblio_contributors to database
 * @param $node
 * @param $update
 * @return success of database operations
 */
function biblio_save_contributors($node, $update = FALSE) {
  if (empty ($node->biblio_contributors)) return true;
  return _save_contributors($node->biblio_contributors, $node->nid, $node->vid, $update);
}
/**
 * Save contributors to the database
 * @param $authors
 * @param $nid
 * @param $vid
 * @param $update
 * @return success of database operations
 */
function _save_contributors($authors, $nid, $vid, $update = FALSE) {
  $rank = 0;
  $md5 = _loadMD5();
  db_query('DELETE FROM {biblio_contributor} WHERE nid = %d AND vid = %d', array($nid, $vid));
  foreach ($authors as $key => $author) {
    if ($update && !empty($author['cid']))  $author['cid'] = null; // null out the cid so we don't do a global change
    if (empty ($author['cid']) && !empty ($md5))
    $author['cid'] = array_search($author['md5'], $md5);
    if (empty ($author['cid'])) {
      if (!drupal_write_record('biblio_contributor_data', $author)) return false;
      $cid = db_last_insert_id('biblio_contributor_data', 'cid');
    } else {
      $cid = $author['cid'];
    }
    $author = (array) $author;
    $link_array = array(
      'nid' => $nid,
      'vid' => $vid,
      'cid' => $cid,
      'rank' => $rank++,
      'auth_type' => $author['auth_type']
    );
    if (!drupal_write_record('biblio_contributor', $link_array)) return false;
  }
  return true; // successfully saved all contributors
}
/*
 Released through http://bibliophile.sourceforge.net under the GPL licence.
 Do whatever you like with this -- some credit to the author(s) would be appreciated.

 A collection of PHP classes to manipulate bibtex files.

 If you make improvements, please consider contacting the administrators at bibliophile.sourceforge.net so that your improvements can be added to the release package.

 Mark Grimshaw 2004/2005
 http://bibliophile.sourceforge.net

 28/04/2005 - Mark Grimshaw.
 Efficiency improvements.

 11/02/2006 - Daniel Reidsma.
 Changes to preg_matching to account for Latex characters in names such as {\"{o}}
 */
// For a quick command-line test (php -f PARSECREATORS.php) after installation, uncomment these lines:
/***********************
 $authors = "Mark \~N. Grimshaw and Bush III, G.W. & M. C. H{\\'a}mmer Jr. and von Frankenstein, Ferdinand Cecil, P.H. & Charles Louis Xavier Joseph de la Vallee P{\\\"{o}}ussin";
 $creator = new PARSECREATORS();
 $creatorArray = $creator->parse($authors);
 print_r($creatorArray);
 ***********************/
/* Create writer arrays from bibtex input.
 'author field can be (delimiters between authors are 'and' or '&'):
 1. <first-tokens> <von-tokens> <last-tokens>
 2. <von-tokens> <last-tokens>, <first-tokens>
 3. <von-tokens> <last-tokens>, <jr-tokens>, <first-tokens>
 */
/**
 * @param $author_array
 * @return unknown_type
 */
function biblio_parse_author(& $author_array) {
  $value = trim($author_array['name']);
  $appellation = $prefix = $surname = $firstname = $initials = '';
  $prefix = "";
  $value = preg_replace("/\s{2,}/", ' ', $value); // replace multiple white space by single space
  $author = explode(",", $value);
  $size = sizeof($author);
  // No commas therefore something like Mark Grimshaw, Mark Nicholas Grimshaw, M N Grimshaw, Mark N. Grimshaw
  if ($size == 1) {
    // Is complete surname enclosed in {...}, unless the string starts with a backslash (\) because then it is
    // probably a special latex-sign..
    // 2006.02.11 DR: in the last case, any NESTED curly braces should also be taken into account! so second
    // clause rules out things such as author="a{\"{o}}"
    //
    if (preg_match("/(.*){([^\\\].*)}/", $value, $matches) && !(preg_match("/(.*){\\\.{.*}.*}/", $value, $matches2))) {
      $author = split(" ", $matches[1]);
      $surname = $matches[2];
    }
    else {
      $author = split(" ", $value);
      // last of array is surname (no prefix if entered correctly)
      $surname = array_pop($author);
    }
  }
  // Something like Grimshaw, Mark or Grimshaw, Mark Nicholas  or Grimshaw, M N or Grimshaw, Mark N.
  else
  if ($size == 2) {
    // first of array is surname (perhaps with prefix)
    list ($surname, $prefix) = _grabSurname(array_shift($author));
  }
  // If $size is 3, we're looking at something like Bush, Jr. III, George W
  else {
    // middle of array is 'Jr.', 'IV' etc.
    $appellation = join(' ', array_splice($author, 1, 1));
    // first of array is surname (perhaps with prefix)
    list ($surname, $prefix) = _grabSurname(array_shift($author));
  }
  $remainder = join(" ", $author);
  list ($firstname, $initials, $prefix2) = _grabFirstnameInitials($remainder);
  if (!empty ($prefix2))
  $prefix .= $prefix2;
  //var_dump($prefix);
  $surname = $surname . ' ' . $appellation;
  $author_array['firstname'] = trim($firstname);
  $author_array['initials'] = trim($initials);
  $author_array['lastname'] = trim($surname);
  $author_array['prefix'] = trim($prefix);
  $author_array['md5'] =  _md5sum($author_array);
  return;
}
/**
 * @param $creator
 * @return unknown_type
 */
function _md5sum($creator) {
  $string = $creator['firstname'] . $creator['initials'] . $creator['prefix'] .$creator['lastname'];
  $string = str_replace(' ', '', drupal_strtolower($string));
  return md5($string);
}
// grab firstname and initials which may be of form "A.B.C." or "A. B. C. " or " A B C " etc.
/**
 * @param $remainder
 * @return unknown_type
 */
function _grabFirstnameInitials($remainder) {
  $prefix = array();
  $firstname = $initials = '';
  $array = split(" ", $remainder);
  foreach ($array as $value) {
    $firstChar = drupal_substr($value, 0, 1);
    if ((ord($firstChar) >= 97) && (ord($firstChar) <= 122)){
    $prefix[] = $value;
    } else if (preg_match("/[a-zA-Z]{2,}/", trim($value))){
      $firstnameArray[] = trim($value);
    } else {
      $initialsArray[] = trim(str_replace(".", " ", trim($value)));
    }
  }
  if (isset ($initialsArray)) {
    $initials = join(" ", $initialsArray);
  }
  if (isset ($firstnameArray)) {
    $firstname = join(" ", $firstnameArray);
  }
  if (!empty ($prefix)){
    $prefix = join(" ", $prefix);
  }
  return array($firstname,$initials,$prefix);
}
// surname may have title such as 'den', 'von', 'de la' etc. - characterised by first character lowercased.  Any
// uppercased part means lowercased parts following are part of the surname (e.g. Van den Bussche)
/**
 * @param $input
 * @return unknown_type
 */
function _grabSurname($input) {
  $surnameArray = split(" ", $input);
  $noPrefix = $surname = FALSE;
  foreach ($surnameArray as $value) {
    $firstChar = substr($value, 0, 1);
    if (!$noPrefix && (ord($firstChar) >= 97) && (ord($firstChar) <= 122))
    $prefix[] = $value;
    else {
      $surname[] = $value;
      $noPrefix = TRUE;
    }
  }
  if ($surname)
  $surname = join(" ", $surname);
  if (isset ($prefix)) {
    $prefix = join(" ", $prefix);
    return array(
    $surname,
    $prefix
    );
  }
  return array(
  $surname,
  FALSE
  );
}
/**
 * @param $type
 * @return unknown_type
 */
function _typeMap($type) {
  static $typeMap = array();
  if (empty ($typeMap)) {
    $result = db_query("SELECT * FROM {biblio_contributor_type} ;");
    while ($type = db_fetch_object($result)) {
      $typeMap[$type->type] = $type->ct_id;
    }
  }
  return $typeMap[$type];
}
/**
 * @return unknown_type
 */
function _loadMD5() {
  static $md5 = array();
  static $count =0;
  $db_count = db_result(db_query("SELECT COUNT(*) FROM {biblio_contributor_data"));
  if ($db_count > $count){
    $count = $db_count;
    $result = db_query('SELECT md5,cid	FROM {biblio_contributor_data} ');
    while ($row = db_fetch_array($result)) {
      $md5[$row['cid']] = $row['md5'];
    }
  }
  return (count($md5)) ? $md5 : NULL;
}