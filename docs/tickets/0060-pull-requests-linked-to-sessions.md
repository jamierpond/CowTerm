# 60 Pull requests linked to their claude sessions

state: in-human
source: ask
owner: cowterm-task-board

Change: `p` on the board shows Jamie's open pull requests updated in the
window (`t` cycles today / 3 days / week), from one `gh api graphql` search
(`is:pr is:open author:@me`, every repository), fetched on a worker and
cached a minute per window. Each row: repo#number and title, checks, review
decision, and the linked session with the rule that linked it and its card's
column, or "ended". Enter goes to the agent's pane; for an ended session it
opens a session in its directory running `claude --resume <id>`. `o` opens
the pull request in the browser. Click selects, a second click or a
double-click is Enter. `ct board prs [--since today|3d|week]` prints the same.

Linking, first that matches, newest session wins within a rule:
1. branch: the session ran on the PR's head branch in a checkout with the
   head repository among its remotes (forks push upstream too).
2. pr-link: claude's own `"type":"pr-link"` transcript record names the PR
   (claude writes one when a session opens a PR). Added beyond the ask: on
   real data the mention rule alone linked all 11 PRs to one session that
   reads iac, where the URLs are posted.
3. mentioned: the URL appears anywhere in the transcript.

Check: CowTermBoardTests PullRequests/* (rules, fork remotes, URL parsing,
GraphQL parsing, windows) and TaskBoardView/pullRequestsGoToTheirAgent pass;
`pull-requests-*.png` and `pull-requests-live-*.png` at five shapes.

Result: 21/21 + 7/7 board tests, 81/81 ctest. Live run over 3 days: 11 PRs, all linked by
pr-link, 4 to the working crash-reporting agent, 7 to ended sessions. None
matched by branch: those PRs' branches were made in worktrees no transcript
ran in. Not proven: Enter/resume and `o` in the running app.
`ct board --since` is spelled `ct board prs --since`.
