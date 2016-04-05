// Copyright (C) 2009 ent( entdark @ mail.ru )

#include "tr_mme.h"

void pipeClose( mmePipeFile_t *pipeFile ) {
    if (!pipeFile->f)
        return;
    ri.FS_PipeClose(pipeFile->f);
}
