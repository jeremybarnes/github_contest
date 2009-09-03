# loadbuild.mk
# Jeremy Barnes, 11 August 2009
# loadbuilding for github contest

JML_BIN := jml/../build/$(ARCH)/bin

loadbuild: results.txt fake-results.txt prob-results.txt

SOURCES := $(shell grep 'sources=' config.txt | sed 's/.*sources=//;s/;//;s/,/ /g')

FAMILY_FEATURES := repo_has_parent repo_num_children repo_num_ancestors repo_num_siblings repo_parent_watchers

IGNORE_FEATURES_authored_by_me := $(FAMILY_FEATURES)
IGNORE_FEATURES_by_watched_authors := $(FAMILY_FEATURES)
IGNORE_FEATURES_same_name := $(FAMILY_FEATURES)
IGNORE_FEATURES_in_cluster_user := $(FAMILY_FEATURES)
IGNORE_FEATURES_in_cluster_repo := $(FAMILY_FEATURES)
IGNORE_FEATURES_in_id_range := $(FAMILY_FEATURES)
IGNORE_FEATURES_coocs := $(FAMILY_FEATURES)
IGNORE_FEATURES_coocs2 := $(FAMILY_FEATURES)
IGNORE_FEATURES_most_watched := $(FAMILY_FEATURES)

define process_source

$$(warning ignoring for $(1) $$(foreach feature,$$(IGNORE_FEATURES_$(1)), --ignore-var $$(feature)))

data/$(1)-fv.txt.gz: data/kmeans_users.txt data/kmeans_repos.txt
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--dump-source-data \
		--source-to-train=generator.$(1) \
		--include-all-correct=1 \
		--num-users=20000 \
		--tranches=10 \
		--output-file $$@~ \
		generator.load_data=false \
		ranker.load_data=false \
	2>&1 | tee $$@.log
	mv $$@~ $$@

data/$(1).cls:	data/$(1)-fv.txt.gz \
		ranker-classifier-training-config.txt
	set -o pipefail && \
	/usr/bin/time \
	$(JML_BIN)/classifier_training_tool \
		--configuration-file ranker-classifier-training-config.txt \
		--group-feature GROUP \
		--weight-spec WT/V \
		--validation-split 20 \
		--testing-split 10 \
		--randomize-order \
		--probabilize-mode=2 \
		--probabilize-weighted=1 \
		--trainer-name phase1 \
		--ignore-var WT \
		--ignore-var GROUP \
		--ignore-var REAL_TEST \
		--testing-filter 'REAL_TEST == 1' \
		-G 2 -C 2 \
		--output-file $$@~ \
		--no-eval-by-group \
		$$(foreach feature,$$(IGNORE_FEATURES_$(1)), --ignore-var $$(feature)) \
		$$< \
	2>&1 | tee $$@.log
	mv $$@~ $$@

PHASE1_FILES += data/$(1).cls

endef

$(foreach source,$(SOURCES),$(eval $(call process_source,$(source))))

results.txt:	prob-results.txt
	set -o pipefail && \
	cat $< \
	| sed 's/{\([0-9]\+\),[0-9.]\+}/\1/g' \
	| awk -F , '{ for (i = 1;  i <= 10 && i < NF;  ++i) printf("%s,", $$i); printf("\n"); }' \
	| sed 's/,$$//g' \
	> $@~
	mv $@~ $@

prob-results.txt: data/ranker.cls
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--dump-results \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@

fake-results.txt: data/ranker.cls
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--fake-test \
		--random-seed 2 \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@
	tail -n20 $@

data/ranker.cls: \
		data/ranker-fv.txt.gz \
		ranker-classifier-training-config.txt
	set -o pipefail && \
	/usr/bin/time \
	$(JML_BIN)/classifier_training_tool \
		--configuration-file ranker-classifier-training-config.txt \
		--group-feature GROUP \
		--weight-spec WT/V \
		--validation-split 20 \
		--testing-split 10 \
		--randomize-order \
		--probabilize-mode=2 \
		--probabilize-weighted=1 \
		--trainer-name default \
		--ignore-var WT \
		--ignore-var GROUP \
		--ignore-var REAL_TEST \
		--testing-filter 'REAL_TEST == 1' \
		-G 2 -C 2 \
		--output-file $@~ \
		$< \
	2>&1 | tee $@.log
	mv $@~ $@

data/ranker-fv.txt.gz: $(PHASE1_FILES)
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--dump-merger-data \
		--include-all-correct=0 \
		--num-users=20000 \
		--output-file $@~ \
		ranker.load_data=false \
		--tranches=01 \
	2>&1 | tee $@.log
	mv $@~ $@


# For both of these, we cause the same (user, repo) pairs to be removed from
# the dataset as in the rest of the training, to avoid problems with the
# number of entries

data/kmeans_users.txt:
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--cluster-users \
		--num-users=20000 \
		--fake-test \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@

data/kmeans_repos.txt:
	set -o pipefail && \
	/usr/bin/time \
	$(BIN)/github \
		--cluster-repos \
		--num-users=20000 \
		--fake-test \
		--output-file $@~ \
	2>&1 | tee $@.log
	mv $@~ $@
