#!/bin/bash 
# A simple script to backup our github repo's. Somewhat modified from a Gist I found online.
#
# The token below was generated by going to Settings -> Developer Settings and then Personal Access Tokens
#
# It was scope down to just allow:
#    - repo: 
#      - repo:public_repo
#      - repo_deployment
#      - public_repo
#      - repo:invite
#    - read:org
#
# and that seems to do the trick.


USER="USERNAME"
TOKEN="TODO"



GHBU_TOKEN=${GHBU_TOKEN-$TOKEN}
GHBU_BACKUP_DIR=${GHBU_BACKUP_DIR-"github-backups"}                  # where to place the backup files
GHBU_ORG=${GHBU_ORG-"ORGNAME"}                                   # the GitHub organization whose repos will be backed up
GHBU_GITHOST=${GHBU_GITHOST-"github.com"}                # the GitHub hostname (see notes)
GHBU_PRUNE_OLD=${GHBU_PRUNE_OLD-true}                                # when `true`, old backups will be deleted
GHBU_PRUNE_AFTER_N_DAYS=${GHBU_PRUNE_AFTER_N_DAYS-3}                 # the min age (in days) of backup files to delete
GHBU_SILENT=${GHBU_SILENT-false}                                     # when `true`, only show error messages 
GHBU_API=${GHBU_API-"https://api.github.com"}                        # base URI for the GitHub API
GHBU_GIT_CLONE_CMD="git clone --quiet --mirror https://${USER}:${TOKEN}@${GHBU_GITHOST}/" # base command to use to clone GitHub repos

TSTAMP=`date "+%Y%m%d-%H%M"`

# The function `check` will exit the script if the given command fails.
function check {
  "$@"
  status=$?
  if [ $status -ne 0 ]; then
    echo "ERROR: Encountered error (${status}) while running the following:" >&2
    echo "           $@"  >&2
    echo "       (at line ${BASH_LINENO[0]} of file $0.)"  >&2
    echo "       Aborting." >&2
    exit $status
  fi
}

# The function `tgz` will create a gzipped tar archive of the specified file ($1) and then remove the original
# the option -P omits the error message tar: Removing leading '/' from member names
function tgz {
   check tar zcPf $1.tar.gz $1 && check rm -rf $1
}

$GHBU_SILENT || (echo "" && echo "=== INITIALIZING ===" && echo "")

$GHBU_SILENT || echo "Using backup directory $GHBU_BACKUP_DIR"
check mkdir -p $GHBU_BACKUP_DIR

$GHBU_SILENT || echo "Fetching list of repositories for ${GHBU_ORG}..."
# cycling through pages as github API limits entries to 30/100 per page...
PAGE=0
while true; do
  let PAGE++
  $GHBU_SILENT || echo "getting page ${PAGE}"
  REPOLIST_TMP=`check curl --silent -H "Authorization: token $GHBU_TOKEN" ${GHBU_API}/orgs/${GHBU_ORG}/repos\?page=${PAGE}\&per_page=90 -q -k | jq '.[].name' | sed 's/\"//g' `
  if [ -z "${REPOLIST_TMP}" ]; then break; fi
  REPOLIST="${REPOLIST} ${REPOLIST_TMP}"
done


$GHBU_SILENT || echo "found `echo $REPOLIST | wc -w` repositories."


$GHBU_SILENT || (echo "" && echo "=== BACKING UP ===" && echo "")

for REPO in $REPOLIST; do
   echo $REPOLIST
   $GHBU_SILENT || echo "Backing up ${GHBU_ORG}/${REPO}"
   check ${GHBU_GIT_CLONE_CMD}${GHBU_ORG}/${REPO}.git ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}-${TSTAMP}.git && tgz ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}-${TSTAMP}.git

   $GHBU_SILENT || echo "Backing up ${GHBU_ORG}/${REPO}.wiki (if any)"
   ${GHBU_GIT_CLONE_CMD}${GHBU_ORG}/${REPO}.wiki.git ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}.wiki-${TSTAMP}.git 2>/dev/null && tgz ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}.wiki-${TSTAMP}.git

   $GHBU_SILENT || echo "Backing up ${GHBU_ORG}/${REPO} issues"
   check curl --silent -H "Authorization: token $GHBU_TOKEN" ${GHBU_API}/repos/${GHBU_ORG}/${REPO}/issues -q > ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}.issues-${TSTAMP} && tgz ${GHBU_BACKUP_DIR}/${GHBU_ORG}-${REPO}.issues-${TSTAMP}
done

if $GHBU_PRUNE_OLD; then
  $GHBU_SILENT || (echo "" && echo "=== PRUNING ===" && echo "")
  $GHBU_SILENT || echo "Pruning backup files ${GHBU_PRUNE_AFTER_N_DAYS} days old or older."
  $GHBU_SILENT || echo "Found `find $GHBU_BACKUP_DIR -name '*.tar.gz' -mtime +$GHBU_PRUNE_AFTER_N_DAYS | wc -l` files to prune."
  find $GHBU_BACKUP_DIR -name '*.tar.gz' -mtime +$GHBU_PRUNE_AFTER_N_DAYS -exec rm -fv {} > /dev/null \; 
fi

$GHBU_SILENT || (echo "" && echo "=== DONE ===" && echo "")
$GHBU_SILENT || (echo "GitHub backup completed." && echo "")
