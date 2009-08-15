# loadbuild.mk
# Jeremy Barnes, 11 August 2009
# loadbuilding for github contest

JML_BIN := jml/../build/$(ARCH)/bin

loadbuild: results.txt fake-results.txt

results.txt: data/ranker.cls
	$(BIN)/github \
		--dump-results \
		--output-file $@~
	mv $@~ $@

fake-results.txt: data/ranker.cls
	$(BIN)/github \
		--fake-test \
		--random-seed 2 \
		--output-file $@~
	mv $@~ $@
	tail -n20 $@

data/ranker.cls: \
		data/ranker-fv.txt.gz \
		ranker-classifier-training-config.txt
	$(JML_BIN)/classifier_training_tool \
		--configuration-file ranker-classifier-training-config.txt \
		--group-feature GROUP \
		--weight-spec WT/V \
		--validation-split 20 \
		--testing-split 10 \
		--randomize-order \
		--trainer-name default \
		--ignore-var WT \
		--ignore-var GROUP \
		--ignore-var REAL_TEST \
		--testing-filter 'REAL_TEST == 1' \
		-G 2 -C 2 \
		--output-file $@~ \
		--no-eval-by-group \
		$<
	mv $@~ $@

data/ranker-fv.txt.gz:
	$(BIN)/github \
		--dump-merger-data \
		--include-all-correct=0 \
		--output-file $@~
	mv $@~ $@

