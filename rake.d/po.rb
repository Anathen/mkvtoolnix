# coding: utf-8

def format_string_for_po str
  return '"' + str.gsub(/"/, '\"') + '"' unless /\\n./.match(str)

  ([ '""' ] + str.split(/(?<=\\n)/).map { |part| '"' + part.gsub(/"/, '\"') + '"' }).join("\n")
end

def unformat_string_for_po str
  str.gsub(/^"|"$/, '').gsub(/\\"/, '"')
end

def read_po file_name
  items   = [ { comments: [] } ]
  msgtype = nil
  line_no = 0
  started = false

  add_line = lambda do |type, to_add|
    items.last[type] ||= []
    items.last[type]  += to_add if to_add.is_a?(Array)
    items.last[type]  << to_add if to_add.is_a?(String)
  end

  IO.readlines(file_name).each do |line|
    line_no += 1
    line.chomp!

    if !line.empty? && !started
      items.last[:line] = line_no
      started           = true
    end

    if line.empty?
      items << {} unless items.last.keys.empty?
      msgtype = nil
      started = false

    elsif items.size == 1
      add_line.call :comments, line

    elsif /^#:\s*(.+)/.match(line)
      add_line.call :sources, $1.split(/\s+/)

    elsif /^#,\s*(.+)/.match(line)
      add_line.call :flags, $1.split(/,\s*/)

    elsif /^#\./.match(line)
      add_line.call :instructions, line

    elsif /^#~/.match(line)
      add_line.call :obsolete, line

    elsif /^#\|/.match(line)
      add_line.call :suggestions, line

    elsif /^#\s/.match(line)
      add_line.call :comments, line

    elsif /^ ( msgid(?:_plural)? | msgstr (?: \[ \d+ \])? ) \s* (.+)/x.match(line)
      type, string = $1, $2
      msgtype      = type.gsub(/\[.*/, '').to_sym

      items.last[msgtype] ||= []
      items.last[msgtype]  << unformat_string_for_po(string)

    elsif /^"/.match(line)
      fail "read_po: #{file_name}:#{line_no}: string entry without prior msgid/msgstr for »#{line}«" unless msgtype
      items.last[msgtype].last << unformat_string_for_po(line)

    else
      fail "read_po: #{file_name}:#{line_no}: unrecognized line type for »#{line}«"

    end
  end

  items.pop if items.last.keys.empty?

  return items
end

def write_po file_name, items
  File.open(file_name, "w") do |file|
    items.each do |item|
      if item[:obsolete]
        file.puts(item[:obsolete].join("\n"))
        file.puts
        next
      end

      if item[:comments] && !item[:comments].empty?
        file.puts(item[:comments].join("\n"))
      end

      if item[:instructions] && !item[:instructions].empty?
        file.puts(item[:instructions].join("\n"))
      end

      if item[:sources] && !item[:sources].empty?
        file.puts(item[:sources].map { |source| "#: #{source}" }.join("\n").gsub(/,$/, ''))
      end

      if item[:flags] && !item[:flags].empty?
        file.puts("#, " + item[:flags].join(", "))
      end

      if item[:suggestions] && !item[:suggestions].empty?
        file.puts(item[:suggestions].join("\n"))
      end

      if item[:msgid]
        file.puts("msgid " + format_string_for_po(item[:msgid].first))
      end

      if item[:msgid_plural]
        file.puts("msgid_plural " + format_string_for_po(item[:msgid_plural].first))
      end

      if item[:msgstr]
        idx = 0

        item[:msgstr].each do |msgstr|
          suffix  = item[:msgid_plural] ? "[#{idx}]" : ""
          idx    += 1
          file.puts("msgstr#{suffix} " + format_string_for_po(msgstr))
        end
      end

      file.puts
    end
  end
end

def normalize_po file
  puts_action "NORMALIZE-PO", file
  write_po file, read_po(file)
end

def replace_po_meta_info orig_metas, transifex_meta, key
  new_value = /"#{key}: \s+ (.+?) \\n"/x.match(transifex_meta)[1]
  # puts "looking for #{key} in #{transifex_meta}"
  # puts "  new val #{new_value}"
  return unless new_value

  orig_metas.each { |meta| meta.gsub!(/"#{key}: \s+ .+? \\n"/x, "\"#{key}: #{new_value}\\n\"") }
end

def merge_po orig_items, updated_items
  translated = Hash[ *updated_items.
    select { |item| item[:msgid] && item[:msgid].first && !item[:msgid].first.empty? && item[:msgstr] && !item[:msgstr].empty? && !item[:msgstr].first.empty? }.
    map    { |item| [ item[:msgid].first, item ] }.
    flatten(1)
  ]

  update_meta_info = false

  orig_items.each do |orig_item|
    next if !orig_item[:msgid] || orig_item[:msgid].empty? || orig_item[:msgid].first.empty?

    updated_item = translated[ orig_item[:msgid].first ]

    next if !updated_item

    next if (orig_item[:msgstr] == updated_item[:msgstr]) && !(orig_item[:flags] || []).include?("fuzzy")

    # puts "UPDATE of msgid " + orig_item[:msgid].first
    # puts "  old " + orig_item[:msgstr].first
    # puts "  new " + updated_item[:msgstr].first

    update_meta_info   = true
    orig_item[:msgstr] = updated_item[:msgstr]

    orig_item[:flags].reject! { |flag| flag == "fuzzy" } if orig_item[:flags]
    orig_item.delete(:suggestions)
  end

  # update_meta_info = true

  if update_meta_info
    orig_meta    = orig_items.first[:comments]
    updated_meta = updated_items.first[:comments].join("")

    %w{PO-Revision-Date Last-Translator Language-Team Plural-Forms}.each { |key| replace_po_meta_info orig_meta, updated_meta, key }
  end

  orig_items
end

def transifex_pull_and_merge resource, language
  po_file = resource == "programs" ? "po/#{language}.po" : "doc/man/po4a/po/#{language}.po"

  runq_git po_file, "checkout HEAD -- #{po_file}"

  orig_items = read_po(po_file)

  runq "tx pull", po_file, "tx pull -f -r mkvtoolnix.#{resource} -l #{language} > /dev/null"

  puts_qaction "merge", po_file

  transifex_items = read_po(po_file)
  merged_items    = merge_po orig_items, transifex_items

  write_po po_file, merged_items
end

def transifex_remove_fuzzy_and_push resource, language
  po_file          = resource == "programs" ? "po/#{language}.po" : "doc/man/po4a/po/#{language}.po"
  po_file_no_fuzzy = Tempfile.new("mkvtoolnix-rake-po-no-fuzzy")

  runq_git po_file, "checkout HEAD -- #{po_file}"

  runq "msgattrib", po_file, "msgattrib --no-fuzzy --output=#{po_file_no_fuzzy.path} #{po_file}"

  IO.write(po_file, IO.read(po_file_no_fuzzy))

  normalize_po po_file

  runq "tx push",  po_file, "tx push -t -f --no-interactive -r mkvtoolnix.#{resource} -l #{language} > /dev/null"

  runq_git po_file, "checkout HEAD -- #{po_file}"
end
