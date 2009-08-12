# loadbuild.mk
# Jeremy Barnes, 11 August 2009
# loadbuilding for github contest

JML_BIN := jml/../build/$(ARCH)/bin

loadbuild: results.txt

results.txt: data/ranker.cls
	$(BIN)/github \
		--dump-results \
		--output-file $@~
	mv $@~ $@

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
		-G 2 -C 2 \
		--output-file $@~ \
		$<
	mv $@~ $@

data/ranker-fv.txt.gz:
	$(BIN)/github \
		--dump-merger-data \
		--output-file $@~
	mv $@~ $@

