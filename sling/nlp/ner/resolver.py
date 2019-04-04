import sling
import sling.flags as flags

flags.define("--start")
flags.define("--lang", default="en")
flags.parse()

formname = ["  ", "UP", "lo", "Ca"]
def compatible(form1, form2):
  if form1 == sling.CASE_NONE: return True
  if form2 == sling.CASE_NONE: return True
  return form1 == form2

def item_text(item):
  s = item.id
  name = item.name
  if name != None: s = s + " " + name
  descr = item.description
  if descr != None:
    if len(descr) > 40:
      s += " (" + descr[:40] + "...)"
    else:
      s += " (" + descr + ")"
  return s

def item_name(item):
  name = item.name
  if name == None: name = item.id
  if name == None: name = "?"
  return name

kb = sling.Store()
kb.lockgc()
kb.load("local/data/e/wiki/kb.sling")
langdir = "local/data/e/wiki/" + flags.arg.lang
phrasetab = sling.PhraseTable(kb, langdir + "/phrase-table.repo")
factex = sling.FactExtractor(kb)
n_page_item = kb["/wp/page/item"]
n_popularity = kb["/w/item/popularity"]
n_fanin = kb["/w/item/fanin"]
n_links = kb["/w/item/links"]
kb.freeze()

form_penalty = 0.1
base_context_score = 1e-3
topic_weight = 1.0
mention_weight = 500.0
thematic_weight = 0.0

num_docs = 0
num_mentions = 0
num_unknown = 0
num_first = 0
num_prior_losses = 0
num_at_rank = [0] * 20

corpus = sling.Corpus(langdir + "/documents@10.rec", commons=kb)
first = True
for doc in corpus:
  if first and flags.arg.start: doc = corpus[flags.arg.start]
  first = False
  num_docs += 1
  print "Document", num_docs, ":", doc.frame["title"]
  store = doc.store
  context = {}

  # Add document entity to context model.
  item = doc.frame[n_page_item]
  context[item] = context.get(item, 0.0) + topic_weight

  #for fact in factex.facts(store, item):
  #  target = fact[-1]
  #  fanin = target[n_fanin]
  #  if fanin != None:
  #    context[target] = context.get(target, 0.0) + 1.0 / fanin

  # Add unanchored links to context model.
  if thematic_weight > 0:
    for theme in doc.themes:
      if type(theme) is not sling.Frame: continue
      theme = theme.resolve()
      popularity = item[n_popularity]
      if popularity == None: popularity = 1
      context[theme] = context.get(theme, 0.0) + thematic_weight / popularity

  # Try to score and rank all linked mentions.
  for mention in doc.mentions:
    # Get phrase and case form.
    phrase = doc.phrase(mention.begin, mention.end)
    initial = mention.begin < len(doc.tokens) and \
              doc.tokens[mention.begin].brk >= sling.SENTENCE_BREAK
    form = phrasetab.form(phrase)
    if initial and form == sling.CASE_TITLE: form = sling.CASE_NONE

    matches = phrasetab.query(phrase)
    for item in mention.evokes():
      if item.isanonymous(): continue;

      # Score all matches.
      scores = []
      score_sum = 0.0
      found = False
      for m in matches:
        # Compute score for match.
        cscore = context.get(m.item(), base_context_score)
        clue = None
        best = base_context_score
        #for fact in factex.facts(store, m.item()):
        #  target = fact[-1]
        #  weight = context.get(target, 0.0)
        #  cscore += weight
        #  if weight > best:
        #    best = weight
        #    clue = target
        links = m.item()[n_links]
        if links != None:
          for link,count in links:
            weight = context.get(link, 0.0)
            cscore += weight * count
        if not compatible(form, m.form()): cscore *= form_penalty
        score = cscore * m.count()
        scores.append((m, score, cscore, clue, best))
        if m.item() == item: found = True

      # Ignore mention if it is not in the prase table.
      if not found:
        #print "N/A", phrase, item_text(item)
        num_unknown += 1
        continue
      num_mentions += 1

      # Sort scores.
      scores.sort(key=lambda t: t[1], reverse=True)

      # Output ranked entities.
      rank = 0
      if scores[0][0].item() != item:
        print phrase, item.id
        for s in scores:
          m = s[0]
          candidate = m.item()
          score = s[1]
          cscore = s[2]
          clue = s[3]
          clue_score = s[4]

          hint = ""
          if clue != None:
            hint = " {" + item_name(clue) + ":" + str(clue_score) + "}"

          print "%11.4f %s %5d %8.4f %s%s" % (score, formname[m.form()],
                                              m.count(), cscore,
                                              item_text(candidate), hint)
          if candidate == item: break
          rank += 1

        if rank + 1 < len(scores): print "... and", len(scores) - rank, "more"

      if rank >= len(num_at_rank): rank = len(num_at_rank) - 1
      num_at_rank[rank] += 1
      if rank > 0 and item == matches[0].item(): num_prior_losses += 1

      # Update context model.
      popularity = item[n_popularity]
      if popularity == None: popularity = 1
      context[item] = context.get(item, 0.0) + mention_weight / popularity
      #for fact in factex.facts(store, item):
      #  target = fact[-1]
      #  fanin = target[n_fanin]
      #  if fanin != None:
      #    context[target] = context.get(target, 0.0) + 1.0 / fanin

      links = item[n_links]
      if links != None:
        for link, count in links:
          popularity = link[n_popularity]
          if popularity == None: popularity = 1
          context[link] = context.get(link, 0.0) + float(count) / popularity

  print len(context), "items in context", len(doc.mentions), "mentions"
  print
  if num_docs == 1000: break

print num_mentions, "mentions"
print num_unknown, "unknown"
print num_prior_losses, "prior losses"
total_mentions = num_mentions + num_unknown
print 100.0 * num_mentions / total_mentions, "% coverage"

cummulative = 0.0
for r in range(0, len(num_at_rank)):
  if num_at_rank[r] == 0: continue
  cummulative += num_at_rank[r]
  print "P@%d: %.2f%%" % (r + 1, 100.0 * cummulative / num_mentions)
